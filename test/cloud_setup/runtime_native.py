"""Actual owner loop/mailbox/NVS/codec with synthetic radio/USB/worker fixtures."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/owner-cloud-runtime';OUT.mkdir(parents=True,exist_ok=True)
if (OUT/'results.json').exists():
 previous=json.loads((OUT/'results.json').read_text());attempt=len(list(OUT.glob('attempt-*.json')))+1
 (OUT/f'attempt-{attempt}.json').write_text(json.dumps(previous,indent=2)+'\n')
 for old in OUT.glob('*.log'):
  if not old.name.startswith('attempt-'):(OUT/f'attempt-{attempt}-{old.name}').write_bytes(old.read_bytes())
sys.path.insert(0,str(ROOT/'tools'))
from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson')
assert json.loads((JSON/'library.json').read_text())['version']=='7.4.3'
assert '#define ARDUINOJSON_VERSION "7.4.3"' in (JSON/'src/ArduinoJson/version.hpp').read_text()
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1:detect_stack_use_after_return=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
baseline=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text();assert baseline.count('\nint main(')==1
(OUT/'runtime-fixture-prefix.h').write_text(baseline.split('\nint main(',1)[0]+'\n')
fixture=OUT/'synthetic-owner.bin';fixture.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
sources=['test/cloud_setup/runtime_native.cpp','src/wifi_setup_nvs.cpp','src/owner_cloud_nvs.cpp','src/config_loader.cpp','src/pro_core.cpp',
 'lib/owner_cloud_setup/owner_cloud_model.cpp','lib/owner_cloud_setup/owner_cloud_store.cpp','lib/wifi_setup_model/wifi_setup_model.cpp',
 'lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp']
inputs=sources+['src/wifi_setup_runtime.cpp','include/owner_cloud_feature.h','include/owner_cloud_nvs.h','include/pro_ble_lifecycle.h',
 'lib/owner_cloud_setup/owner_cloud_model.h','lib/owner_cloud_setup/owner_cloud_store.h',
 'test/cloud_setup/runtime_stubs.h','test/cloud_setup/runtime_stubs/Preferences.h','test/cloud_setup/runtime_stubs/config_loader.h','test/cloud_setup/runtime_native.py']
hashes={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in inputs};records=[]
def publish(final=False):
 stable=all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h for n,h in hashes.items())
 (OUT/'results.json').write_text(json.dumps(dict(passed=final and stable and all(r['passed'] for r in records),complete=final,
   source_stable=stable,desktop_only=True,physical_device=False,real_cloud=False,simulated=['Wi-Fi','USB','BLE admission','worker'],
   actual=['owner runtime/mailbox','config loader/validation','cloud codec','NVS adapter'],source_sha256=hashes,records=records),indent=2)+'\n')
 return stable
def run(name,cmd):
 p=subprocess.run(cmd,cwd=ROOT,env=ENV,text=True,capture_output=True,timeout=180)
 text=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(text)
 diag=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 passed=p.returncode==0 and not p.stderr and not diag
 records.append(dict(name=name,command=cmd,exit=p.returncode,passed=passed,sanitizer_diagnostic=diag));publish();print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:print(text[-10000:]);raise SystemExit(1)
flags=['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
 '-pthread','-Itest/cloud_setup/runtime_stubs','-Itest/wifi_integration/stubs','-Itest/backup_integration','-Iinclude','-Ilib/wifi_setup_model','-Ilib/owner_cloud_setup','-I'+str(OUT),
 '-isystem',str(JSON/'src'),'-isystem',str(DEPS/'include'),'-include','test/cloud_setup/runtime_stubs.h']
defines=['ARDUINOJSON_POOL_CAPACITY=128','CSP_ENABLE_FILE_HISTORY=1','CSP_ENABLE_OWNER_CLOUD_SETUP=1','CSP_ENABLE_BACKUP_SETUP=1','CSP_ENABLE_WIFI_SETUP=1','CSP_ENABLE_BLE_MANAGEMENT=1','CSP_USB_BENCH_AUTOMATION=1',
 'CONFIG_BT_NIMBLE_ENABLED=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
binary=str(OUT/'runtime');clang=str(DEPS/'lib/llvm-18/bin/clang++')
run('build',[clang,*flags,*['-D'+d for d in defines],*sources,'-L'+str(DEPS/'lib/x86_64-linux-gnu'),'-lmbedx509','-lmbedcrypto','-o',binary])
for scenario in ['new-board','keep-secrets','unauthenticated','wifi-mutual-exclusion','abort','disconnect','auth-loss','heap-low','largest-low','no-consent','no-wifi','stale-edit','physical-expiry','raw-conflict','unknown-commit']:
 run(scenario,[binary,str(fixture),scenario])
if not publish(final=True):raise SystemExit('Source changed during run; stable acceptance required')
