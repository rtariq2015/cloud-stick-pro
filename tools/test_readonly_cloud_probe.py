"""Actual DAV adapter with bounded synthetic HTTP/TLS; no device/cloud access."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/public-wifi/read-only-probe'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
         LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
records=[]
sources=['include/webdav_client.h','src/webdav_client.cpp',
         'test/public_wifi_probe/readonly_probe.test.cpp','tools/test_readonly_cloud_probe.py',
         *[str(p.relative_to(ROOT)) for p in (ROOT/'test/public_wifi_probe/stubs').glob('*.h')]]
hashes={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
def run(name,command):
    result=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
    output=result.stdout+result.stderr;(OUT/(name+'.log')).write_text(output)
    diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    private_absent=all(text not in output for text in ['synthetic-probe-user','synthetic-probe-password','synthetic-trusted-ca'])
    passed=result.returncode==0 and not result.stderr and not diagnostic and private_absent
    records.append({'name':name,'command':command,'exit':result.returncode,'stderr_empty':not result.stderr,
                    'sanitizer_diagnostic':diagnostic,'synthetic_credentials_absent_from_output':private_absent,'passed':passed})
    (OUT/'results.json').write_text(json.dumps({'passed':all(r['passed'] for r in records),
        'desktop_only':True,'physical_device_access':False,'real_cloud_access':False,
        'actual_adapter':'src/webdav_client.cpp','simulated':['HTTP','TLS verification','clock','Wi-Fi'],
        'source_sha256':hashes,'records':records},indent=2)+'\n')
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:print(output[-10000:]);raise SystemExit(1)
for flag in [0,1]:
    binary=OUT/f'readonly-probe-{flag}'
    command=[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',
      '-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',
      '-fno-omit-frame-pointer',f'-DCSP_ENABLE_BACKUP_SETUP={flag}',
      '-Itest/public_wifi_probe/stubs','-Itest/backup_integration/dav_stubs','-Iinclude','-Ilib/tinyxml2/src',
      'src/webdav_client.cpp','src/pro_core.cpp','lib/tinyxml2/src/tinyxml2.cpp',
      'test/public_wifi_probe/readonly_probe.test.cpp','-Wl,--wrap=fopen','-Wl,--wrap=time','-lcrypto','-o',str(binary)]
    if flag:command[1:1]=['-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1']
    run(f'build-{flag}',command);run(f'probe-{flag}',[str(binary)])
run('existing-default-and-combined-dav',['python3','test/backup_integration/dav_cancel.test.py'])
assert all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==value for name,value in hashes.items())
