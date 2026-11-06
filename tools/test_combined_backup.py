"""Strict source-only integration gates. Never contact serial/cloud/user media."""
from pathlib import Path
import hashlib,json,os,re,subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/backup-wifi-integration/final-regressions'
OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
env=dict(os.environ,PLATFORMIO_CORE_DIR='/config/.unmanic/research/cloud-stick-platformio',
 PATH=str(DEPS/'lib/llvm-18/bin')+os.pathsep+os.environ['PATH'],
 C_INCLUDE_PATH=str(DEPS/'include'),LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
 UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
 CSP_WIFI_TEST_OUT='artifacts/backup-wifi-integration/m2-regressions',
 CSP_WORKER_TEST_OUTPUT=str(OUT/'worker'))
commands=[
 ('production-core',['bash','test/run.sh'],'curl: (52) Empty reply from server\n'*2),
 ('ble-policy',['bash','test/run_ble_native.sh'],''),
 ('worker',['bash','test/backup_integration/run_worker_tests.sh'],''),
 ('memory-observer',['python3','test/backup_integration/memory_observer.test.py'],''),
 ('wifi-buffer-policy',['python3','test/backup_integration/wifi_buffer.test.py'],''),
 ('backup-wording',['python3','test/backup_integration/backup_wording_tests.py'],''),
 ('owned-route-disabled-prototype',['python3','test/owned_guest_route/run_tests.py'],''),
 ('dav',['python3','test/backup_integration/dav_cancel.test.py'],''),
 ('config',['python3','test/backup_integration/config_loader.test.py'],''),
 ('runtime',['python3','tools/test_backup_runtime.py'],''),
 ('phone',['python3','test/backup_integration/phone_backup_tests.py'],''),
 ('features',['python3','test/wifi_integration/bench_feature_flags.test.py'],''),
 ('wifi-runtime-sdk-build',['python3','tools/test_wifi_integration.py'],''),
 ('wifi-sdk-protocol',['node','--test','test/wifi_integration/connection-sdk.test.mjs'],''),
 ('sdk-lifecycle',['python3','test/milestone1/test_sdk_lifecycle.py'],''),
 ('native-bridge',['python3','test/wifi_integration/native_bluez.test.py'],None),
]
files=sorted(p for prefix in ['include','src','lib/wifi_setup_model','phone/wifi-setup',
                              'test/backup_integration','test/owned_guest_route']
             for p in (ROOT/prefix).rglob('*') if p.is_file() and p.suffix in
             ['.h','.cpp','.mjs','.py','.sh'])
hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
records=[]
for name,command,expected_stderr in commands:
 r=subprocess.run(command,cwd=ROOT,env=env,text=True,capture_output=True,timeout=600)
 output=r.stdout+r.stderr;(OUT/(name+'.log')).write_text(output)
 diagnostics=bool(re.search(r'ERROR: (?:AddressSanitizer|LeakSanitizer)|runtime error:|SUMMARY: (?:AddressSanitizer|UndefinedBehaviorSanitizer)|AddressSanitizer:DEADLYSIGNAL',output))
 stderr_ok=(r.stderr==expected_stderr) if expected_stderr is not None else bool(re.fullmatch(r'\.{5}\n-+\nRan 5 tests in [0-9.]+s\n\nOK\n',r.stderr))
 passed=r.returncode==0 and stderr_ok and not diagnostics
 records.append({'name':name,'command':command,'exit':r.returncode,
                 'stderr_expected':stderr_ok,'sanitizer_diagnostic':diagnostics,'passed':passed})
 unchanged=all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h for n,h in hashes.items())
 (OUT/'results.json').write_text(json.dumps({'desktop_only':True,'physical_device_access':False,
   'real_cloud_access':False,'source_sha256':hashes,'sources_unchanged':unchanged,
   'strict_sanitizers':True,'records':records,'passed':all(x['passed'] for x in records) and unchanged},indent=2)+'\n')
 print(('PASS' if passed and unchanged else 'FAIL'),name,flush=True)
 if not passed or not unchanged:
  print(output[-5000:],flush=True);raise SystemExit(1)
