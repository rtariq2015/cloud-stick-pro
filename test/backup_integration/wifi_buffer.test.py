"""Test actual wrapper code against captured synthetic driver configuration."""
from pathlib import Path
import json
import os
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/backup-wifi-integration/wifi-buffer-policy'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
         LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
flags=['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
       '-fno-sanitize-recover=all','-fno-omit-frame-pointer',
       '-Itest/backup_integration/wifi_buffer_stubs','-Iinclude']
records=[]
def run(name,command):
    result=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=60)
    output=result.stdout+result.stderr;(OUT/(name+'.log')).write_text(output)
    diagnostics=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    passed=result.returncode==0 and not result.stderr and not diagnostics
    records.append({'name':name,'command':list(map(str,command)),'exit':result.returncode,
                    'sanitizer_diagnostic':diagnostics,'stderr_empty':not result.stderr,'passed':passed})
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:print(output);raise RuntimeError(name)
    return result.stdout
try:
    run('default-off-build',[CLANG,*flags,'-c','src/wifi_buffer_policy.cpp','-o',OUT/'default-off.o'])
    symbols=run('default-off-symbols',['nm','--defined-only',OUT/'default-off.o'])
    assert '__wrap_esp_wifi_init' not in symbols and '__real_esp_wifi_init' not in symbols
    run('combined-build',[CLANG,*flags,'-DCSP_ENABLE_BACKUP_SETUP=1',
        '-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1',
        'src/wifi_buffer_policy.cpp','test/backup_integration/wifi_buffer.test.cpp','-o',OUT/'wifi-buffer-tests'])
    run('combined-tests',[OUT/'wifi-buffer-tests'])
finally:
    (OUT/'results.json').write_text(json.dumps({'passed':len(records)==4 and all(row['passed'] for row in records),
        'desktop_only':True,'actual_wrapper_source':True,'SDK_driver_simulated':True,
        'physical_device_access':False,'real_cloud_access':False,'records':records},indent=2)+'\n')
