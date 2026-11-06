"""Future default-off routing adapter tests; all radio/events/sockets are synthetic."""
from pathlib import Path
import json
import os
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/owned-guest-route'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
binary=OUT/'route-tests'
compile=[str(CLANG),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
         '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
         '-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-Itest/owned_guest_route/stubs','-Iinclude',
         'src/owned_guest_route.cpp','test/owned_guest_route/route.test.cpp','-o',str(binary)]
commands=[('build',compile),('normal',[str(binary)])]
for mode in ['disable','apstop','stop-event','barrier','unregister','close','registration']:
    commands.append(('fault-'+mode,[str(binary),mode]))
disabled=OUT/'disabled.o'
commands.append(('default-off',[str(CLANG),'-std=c++17','-Iinclude','-c','src/owned_guest_route.cpp','-o',str(disabled)]))
records=[]
for name,command in commands:
    result=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=120)
    output=result.stdout+result.stderr;(OUT/(name+'.log')).write_text(output)
    diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    secrets_absent=all(s not in output for s in ['synthetic-private-key','synthetic-ap'])
    passed=result.returncode==0 and not result.stderr and not diagnostic and secrets_absent
    records.append({'name':name,'command':command,'exit':result.returncode,'passed':passed,
                    'sanitizer_diagnostic':diagnostic,'synthetic_credentials_absent':secrets_absent})
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:
        (OUT/'results.json').write_text(json.dumps({'passed':False,'records':records},indent=2)+'\n')
        print(output);raise SystemExit(1)
symbols=subprocess.run(['nm','-C',str(disabled)],capture_output=True,text=True,check=True).stdout
assert not symbols.strip(), 'Default-off object must define no routing/data symbols'
(OUT/'results.json').write_text(json.dumps({'passed':True,'desktop_only':True,'default_off_symbols_empty':True,
    'physical_device_access':False,'real_network_access':False,'real_cloud_access':False,
    'actual_adapter':'src/owned_guest_route.cpp','records':records},indent=2)+'\n')
