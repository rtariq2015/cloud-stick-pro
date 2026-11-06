"""Source-only wording checks; no image build, publication or device access."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/backup-wifi-integration/backup-wording'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
         LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
commands=[('lcd-build',[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-g','-O1',
    '-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
    '-fno-omit-frame-pointer','-Iinclude','-I.pio/libdeps/tdongle_s3/TFT_eSPI',
    'test/backup_integration/backup_wording.test.cpp','-o',str(OUT/'lcd-wording-tests')]),
    ('lcd-tests',[str(OUT/'lcd-wording-tests')]),
    ('phone-wording',['node','--test','--test-reporter=tap','test/backup_integration/backup_wording_ui.test.mjs']),
    ('existing-phone-regressions',['node','--test','--test-reporter=tap',
      'test/backup_integration/phone_backup_ui.test.mjs',
      'test/backup_integration/phone_backup_admission.test.mjs',
      'test/wifi_integration/phone-ui.test.mjs']),
    ('app-syntax',['node','--check','phone/wifi-setup/app.mjs'])]
files=['phone/wifi-setup/app.mjs','include/pro_display.h',
       'test/backup_integration/backup_wording.test.cpp',
       'test/backup_integration/backup_wording_ui.test.mjs',
       'test/backup_integration/backup_wording_tests.py']
sources={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files}
records=[]
for name,command in commands:
    run=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
    output=run.stdout+run.stderr;(OUT/(name+'.log')).write_text(output)
    diagnostic=bool(re.search(r'runtime error:|AddressSanitizer:|LeakSanitizer:|UndefinedBehaviorSanitizer:',output))
    passed=run.returncode==0 and not run.stderr and not diagnostic
    records.append({'name':name,'command':command,'exit':run.returncode,
                    'stderr_empty':not run.stderr,'sanitizer_diagnostic':diagnostic,'passed':passed})
    (OUT/'results.json').write_text(json.dumps({'passed':all(row['passed'] for row in records),
        'desktop_only':True,'physical_device_access':False,'real_cloud_access':False,
        'published':False,'actual_page_script':True,'pinned_font2_bounds':True,
        'source_sha256':sources,'records':records},indent=2)+'\n')
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:
        print(output[-10000:]);raise SystemExit(1)
assert all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==value for name,value in sources.items())
