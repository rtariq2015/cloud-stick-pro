"""Exercise the actual DAV adapter with synthetic TLS/HTTP/files, never hardware."""
from pathlib import Path
import json
import os
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/backup-wifi-integration/dav-cancel'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
         LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
records=[]
for flag in [0,1]:
    binary=OUT/f'dav-cancel-{flag}'
    compile=[str(CLANG),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',
             '-Wno-misleading-indentation','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer',
             f'-DCSP_ENABLE_BACKUP_SETUP={flag}',
             '-Itest/backup_integration/dav_stubs','-Iinclude','-Ilib/tinyxml2/src',
             'src/webdav_client.cpp','src/pro_core.cpp','lib/tinyxml2/src/tinyxml2.cpp',
             'test/backup_integration/dav_cancel.test.cpp','-lcrypto','-Wl,--wrap=fopen',
             '-o',str(binary)]
    if flag:
        compile[1:1]=['-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1']
    for name,command in [(f'build-{flag}',compile),(f'run-{flag}',[str(binary)])]:
        run=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=120)
        output=run.stdout+run.stderr;(OUT/(name+'.log')).write_text(output)
        diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
        secrets_absent=all(secret not in output for secret in ['synthetic-user','synthetic-secret','synthetic-ca'])
        passed=run.returncode==0 and not run.stderr and not diagnostic and secrets_absent
        records.append({'name':name,'command':command,'exit':run.returncode,
                        'sanitizer_diagnostic':diagnostic,'stderr_empty':not run.stderr,
                        'synthetic_credentials_absent_from_output':secrets_absent,'passed':passed})
        print(name,'PASS' if passed else 'FAIL',flush=True)
        if not passed:
            (OUT/'results.json').write_text(json.dumps({'passed':False,'records':records},indent=2)+'\n')
            print(output);raise SystemExit(1)
(OUT/'results.json').write_text(json.dumps({'passed':True,'desktop_only':True,
    'actual_adapter':'src/webdav_client.cpp','physical_device_access':False,
    'real_cloud_access':False,'synthetic_file_wrapper':True,'records':records},indent=2)+'\n')
