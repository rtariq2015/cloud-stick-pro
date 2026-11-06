"""Source-only regressions for the radio cleanup experiment. No device/cloud."""
from pathlib import Path
import json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/wifi-radio-release3/regressions';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
env=dict(os.environ,PLATFORMIO_CORE_DIR='/config/.unmanic/research/cloud-stick-platformio',
         PATH=str(DEPS/'lib/llvm-18/bin')+os.pathsep+os.environ['PATH'],
         C_INCLUDE_PATH=str(DEPS/'include'),LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
         LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
commands=[('production-core',['bash','test/run.sh'],'curl: (52) Empty reply from server\n'*2),
          ('ble-policy',['bash','test/run_ble_native.sh'],''),
          ('sdk-lifecycle',['python3','test/milestone1/test_sdk_lifecycle.py'],''),
          ('native-bridge',['python3','test/wifi_integration/native_bluez.test.py'],None),
          ('feature-flags',['python3','test/wifi_integration/bench_feature_flags.test.py'],''),
          ('native-js-syntax',['node','--check','tools/native_ble/run.mjs'],'')]
records=[]
for name,command,expected in commands:
    result=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=300)
    (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
    stderr_ok=result.stderr==expected if expected is not None else bool(re.fullmatch(
        r'\.{5}\n-+\nRan 5 tests in [0-9.]+s\n\nOK\n',result.stderr))
    passed=result.returncode==0 and stderr_ok
    records.append({'name':name,'command':command,'exit':result.returncode,'passed':passed,
                    'stderr_matches_expected':stderr_ok})
    (OUT/'results.json').write_text(json.dumps({'passed':all(r['passed'] for r in records),
        'desktop_only':True,'physical_device_access':False,'records':records},indent=2)+'\n')
    print('PASS' if passed else 'FAIL',name,flush=True)
    if not passed:raise SystemExit(1)
