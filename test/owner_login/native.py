"""Fatal-sanitizer pure owner-login policy and production wire tests; offline."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/owner-login-native';OUT.mkdir(parents=True,exist_ok=True)
if (OUT/'results.json').exists():
 attempt=len(list(OUT.glob('attempt-*.json')))+1;(OUT/f'attempt-{attempt}.json').write_bytes((OUT/'results.json').read_bytes())
 for old in OUT.glob('*.log'):
  if not old.name.startswith('attempt-'):(OUT/f'attempt-{attempt}-{old.name}').write_bytes(old.read_bytes())
sys.path.insert(0,str(ROOT/'tools'))
from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
from pinned_test_dependencies import arduino_json_include
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr');JSON=arduino_json_include(ROOT)
fixture=OUT/'synthetic-owner.bin';fixture.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
sources=['test/owner_login/model_wire.cpp','lib/owner_login/owner_login_model.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/owner_record.cpp']
inputs=sources+['include/owner_login_feature.h','lib/owner_login/owner_login_model.h','lib/wifi_setup_model/wifi_wire.h','test/owner_login/native.py']
hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest()for f in inputs};records=[]
def publish(final=False):
 stable=all(hashlib.sha256((ROOT/f).read_bytes()).hexdigest()==h for f,h in hashes.items())
 (OUT/'results.json').write_text(json.dumps(dict(passed=final and stable and all(x['passed']for x in records),complete=final,source_stable=stable,physical=False,real_account=False,source_sha256=hashes,records=records),indent=2)+'\n');return stable
def run(name,cmd):
 p=subprocess.run(cmd,cwd=ROOT,env=ENV,text=True,capture_output=True,timeout=180);text=p.stdout+p.stderr
 (OUT/(name+'.log')).write_text(text);diag=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 ok=p.returncode==0 and not p.stderr and not diag;records.append(dict(name=name,passed=ok,exit=p.returncode,command=cmd,sanitizer_diagnostic=diag));publish();print(name,'PASS'if ok else'FAIL',flush=True)
 if not ok:print(text[-10000:]);raise SystemExit(1)
flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Iinclude','-Ilib/wifi_setup_model','-Ilib/owner_login','-isystem',str(JSON)]
publish()
for mode in (0,1):
 binary=str(OUT/f'native-{mode}');defines=[f'CSP_ENABLE_OWNER_LOGIN={mode}','CSP_ENABLE_WIFI_SETUP=1','CSP_ENABLE_BLE_MANAGEMENT=1','CSP_ENABLE_BACKUP_SETUP=1']
 run(f'build-{mode}',[str(DEPS/'lib/llvm-18/bin/clang++'),*flags,*['-D'+d for d in defines],*sources,'-o',binary]);run(f'run-{mode}',[binary,str(fixture)])
if not publish(True):raise SystemExit('Source changed during run')
