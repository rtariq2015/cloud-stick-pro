"""Actual read-only backup config adapter with synthetic NVS and public CA only."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/backup-wifi-integration/config-loader'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
CA=Path('/etc/ssl/certs/ISRG_Root_X1.pem')
assert CA.is_file(), 'Public system test CA missing; do not substitute a private certificate'
candidates=[]
for library in sorted((ROOT/'.pio/libdeps').glob('*/ArduinoJson')):
    metadata=library/'library.json';version=library/'src/ArduinoJson/version.hpp'
    if metadata.is_file() and version.is_file() and json.loads(metadata.read_text()).get('version')=='7.4.3' and '#define ARDUINOJSON_VERSION "7.4.3"' in version.read_text():
        candidates.append(library/'src')
assert candidates, 'Build a pinned project environment to install ArduinoJson 7.4.3 first'
ARDUINO=candidates[0]
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
binary=OUT/'config-loader-tests'
command=[str(CLANG),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
         '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
         '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1',
         '-Itest/backup_integration/config_stubs','-Iinclude','-Ilib/wifi_setup_model',
         '-I'+str(ARDUINO),'-I'+str(DEPS/'include'),
         'src/config_loader.cpp','src/pro_core.cpp','lib/wifi_setup_model/wifi_setup_model.cpp',
         'test/backup_integration/config_loader.test.cpp','-L'+str(DEPS/'lib/x86_64-linux-gnu'),
         '-lmbedx509','-lmbedcrypto','-o',str(binary)]
records=[]
for name,cmd in [('build',command),('run',[str(binary),str(CA)])]:
    result=subprocess.run(cmd,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=120)
    output=result.stdout+result.stderr
    (OUT/(name+'.log')).write_text(output)
    diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    secrets_absent=all(s not in output for s in ['synthetic-cloud-user','synthetic-cloud-secret','old-station-secret-marker'])
    passed=result.returncode==0 and not result.stderr and not diagnostic and secrets_absent
    records.append({'name':name,'command':cmd,'exit':result.returncode,'passed':passed,
                    'sanitizer_diagnostic':diagnostic,'synthetic_secrets_absent':secrets_absent})
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:
        (OUT/'results.json').write_text(json.dumps({'passed':False,'records':records},indent=2)+'\n')
        print(output);raise SystemExit(1)
(OUT/'results.json').write_text(json.dumps({'passed':True,'desktop_only':True,
    'actual_adapter':'src/config_loader.cpp','actual_json':'ArduinoJson 7.4.3',
    'actual_json_include':str(ARDUINO.relative_to(ROOT)),
    'actual_x509':'host mbedTLS 2.28.8 (target backend remains a physical-test prerequisite)',
    'public_ca_sha256':hashlib.sha256(CA.read_bytes()).hexdigest(),
    'physical_device_access':False,'real_cloud_access':False,'records':records},indent=2)+'\n')
