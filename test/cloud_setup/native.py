"""Strict native model/actual codec tests only; no devices, network or accounts."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/owner-cloud-native';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson')
assert json.loads((JSON/'library.json').read_text())['version']=='7.4.3'
assert '#define ARDUINOJSON_VERSION "7.4.3"' in (JSON/'src/ArduinoJson/version.hpp').read_text()
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
flags=['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-fno-omit-frame-pointer','-Iinclude','-Ilib/wifi_setup_model','-Ilib/owner_cloud_setup','-isystem',str(JSON/'src')]
defines=['CSP_ENABLE_OWNER_CLOUD_SETUP=1','CSP_ENABLE_BACKUP_SETUP=1','CSP_ENABLE_WIFI_SETUP=1','CSP_ENABLE_BLE_MANAGEMENT=1']
names=['include/owner_cloud_feature.h','lib/owner_cloud_setup/owner_cloud_model.h','lib/owner_cloud_setup/owner_cloud_model.cpp',
 'lib/wifi_setup_model/wifi_wire.h','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/secure_json.h',
 'lib/wifi_setup_model/wifi_setup_model.cpp','test/cloud_setup/model_native.cpp','test/cloud_setup/wire_native.cpp','test/cloud_setup/native.py']
hashes={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names};records=[]
def publish(final=False):
 stable=all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h for n,h in hashes.items())
 (OUT/'results.json').write_text(json.dumps(dict(passed=final and stable and all(r['passed'] for r in records),
  complete=final,source_stable=stable,desktop_only=True,hardware=False,real_cloud=False,source_sha256=hashes,records=records),indent=2)+'\n')
 return stable
def run(name,command):
 p=subprocess.run(command,cwd=ROOT,env=ENV,text=True,capture_output=True,timeout=180)
 output=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(output)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
 passed=p.returncode==0 and not p.stderr and not diagnostic
 records.append(dict(name=name,exit=p.returncode,passed=passed,sanitizer_diagnostic=diagnostic,command=command));publish()
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:print(output[-8000:]);raise SystemExit(1)
clang=str(DEPS/'lib/llvm-18/bin/clang++');model=str(OUT/'model')
run('model-build',[clang,*flags,'test/cloud_setup/model_native.cpp','lib/owner_cloud_setup/owner_cloud_model.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','-o',model])
run('model',[model])
for mode in ['enabled','disabled']:
 feature=['-D'+d for d in defines] if mode=='enabled' else []
 binary=str(OUT/('wire-'+mode))
 run('wire-'+mode+'-build',[clang,*flags,*feature,'test/cloud_setup/wire_native.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','-o',binary])
 run('wire-'+mode,[binary])
if not publish(final=True):raise SystemExit('Source changed: final native acceptance failed; stable rerun required')
