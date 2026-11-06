"""Strict offline tests of boot preparation; never contacts a physical device."""
from pathlib import Path
import json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/platform-bootstrap';OUT.mkdir(exist_ok=True)
deps=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
env.update(C_INCLUDE_PATH=str(deps/'include'),LIBRARY_PATH=str(deps/'lib/x86_64-linux-gnu'),
           LD_LIBRARY_PATH=str(deps/'lib/x86_64-linux-gnu'))
flags=['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
 '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DCSP_ENABLE_BLE_MANAGEMENT=1',
 '-DCSP_BLE_ISOLATED_TEST=1','-DCSP_BLE_SYNTHETIC_ONLY=1',
 '-Iinclude','-Itest/platform_prepare_stubs','-I'+str(deps/'include'),
 'src/ble_platform_prepare.cpp','test/platform_prepare.test.cpp']
results=[]
def run(name,args):
 r=subprocess.run(args,cwd=ROOT,env=env,capture_output=True,text=True,timeout=60)
 (OUT/(name+'.log')).write_text(r.stdout+r.stderr)
 results.append({'name':name,'command':list(map(str,args)),'exit':r.returncode,'stderr_empty':not r.stderr})
 if r.returncode or r.stderr:raise RuntimeError(name+' failed; see log')
 print('PASS',name,flush=True)
for real in [False,True]:
 binary=OUT/('prepare-real' if real else 'prepare-faults')
 run(binary.name+'-build',flags+(['-DCSP_USE_REAL_CRYPTO=1','-lmbedcrypto'] if real else [])+['-o',str(binary)])
 for case in (['ok','existing-event'] if real else ['ok','existing-event','event-fail','mpi-fail','gcm-fail','sha-fail',
   'controller-already-active','controller-init-fail','controller-partial-fail',
   'controller-init-no-state','controller-deinit-fail','controller-deinit-stuck']):
  run(binary.name+'-'+case,[str(binary),case])
(OUT/'preparation-tests.json').write_text(json.dumps({'passed':True,'physical_device_access':False,
 'commands':results,'sanitizers':'Nonrecovering ASan/UBSan, leaks enabled, any stderr fails',
 'scope':'Actual preparation module with simulated ESP event/log/controller; real desktop mbedTLS 2.28 KATs and injected failures. Pinned target 3.6/hardware execution pending.'},indent=2)+'\n')
