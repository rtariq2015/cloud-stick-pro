"""Production owner-login runtime/mailbox and private NVS adapters with fakes."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
INTEGRATED=os.environ.get('CSP_OWNER_LOGIN_INTEGRATED')=='1'
OUT=ROOT/('artifacts/owner-login-integrated-runtime' if INTEGRATED else 'artifacts/owner-login-runtime');OUT.mkdir(parents=True,exist_ok=True)
if (OUT/'results.json').exists():
 attempt=len(list(OUT.glob('attempt-*.json')))+1;(OUT/f'attempt-{attempt}.json').write_bytes((OUT/'results.json').read_bytes())
 for old in OUT.glob('*.log'):
  if not old.name.startswith('attempt-'):(OUT/f'attempt-{attempt}-{old.name}').write_bytes(old.read_bytes())
sys.path.insert(0,str(ROOT/'tools'))
from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
from pinned_test_dependencies import arduino_json_include
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr');JSON=arduino_json_include(ROOT)
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1:detect_stack_use_after_return=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
baseline=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text().split('\nint main(',1)[0]
assert baseline.count('BleManagementStatus ble_management_status(){return {};}')==1
baseline=baseline.replace('BleManagementStatus ble_management_status(){return {};}', 'BleManagementStatus fixtureBaselineBleStatus(){return {};}')
(OUT/'runtime-fixture-prefix.h').write_text(baseline+'\n')
(OUT/'cloud-runtime-prefix.h').write_text((ROOT/'test/cloud_setup/runtime_native.cpp').read_text().split('\nint main(',1)[0]+'\n')
old=OUT/'synthetic-old.bin';new=OUT/'synthetic-new.bin';old.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD));new.write_bytes(create_record('001122334455','synthetic-new-password-for-runtime'))
sources=['test/owner_login/runtime_native.cpp','src/wifi_setup_nvs.cpp','src/owner_cloud_nvs.cpp','src/owner_login_nvs.cpp','src/config_loader.cpp','src/pro_core.cpp',
 'lib/owner_cloud_setup/owner_cloud_model.cpp','lib/owner_cloud_setup/owner_cloud_store.cpp','lib/owner_login/owner_login_model.cpp','lib/owner_login/owner_login_store.cpp',
 'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp']
inputs=sources+['src/wifi_setup_runtime.cpp','include/owner_login_feature.h','include/owner_login_nvs.h','include/wifi_setup_runtime.h','lib/owner_login/owner_login_model.h','lib/owner_login/owner_login_store.h','lib/wifi_setup_model/wifi_wire.h','test/owner_login/runtime_native.py','test/owner_login/integrated_stubs.h','test/cloud_setup/runtime_native.cpp','test/cloud_setup/runtime_stubs.h','test/backup_integration/runtime_handoff.test.cpp']
hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest()for f in inputs};records=[]
for directory in ['include','lib/wifi_setup_model','lib/owner_cloud_setup','lib/owner_login']:
 for header in (ROOT/directory).glob('*.h'):hashes[str(header.relative_to(ROOT))]=hashlib.sha256(header.read_bytes()).hexdigest()
def publish(final=False):
 stable=all(hashlib.sha256((ROOT/f).read_bytes()).hexdigest()==h for f,h in hashes.items())
 (OUT/'results.json').write_text(json.dumps(dict(passed=final and stable and all(x['passed']for x in records),complete=final,source_stable=stable,physical=False,real_account=False,simulated=['BLE admission','ACK encryption notification','USB','Wi-Fi','worker','NVS faults'],actual=['owner runtime','mailbox','wire','config decoder','NVS adapters'],source_sha256=hashes,records=records),indent=2)+'\n');return stable
def run(name,cmd):
 p=subprocess.run(cmd,cwd=ROOT,env=ENV,text=True,capture_output=True,timeout=180);text=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(text)
 diag=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text));ok=p.returncode==0 and not p.stderr and not diag
 records.append(dict(name=name,passed=ok,exit=p.returncode,command=cmd,sanitizer_diagnostic=diag));publish();print(name,'PASS'if ok else'FAIL',flush=True)
 if not ok:print(text[-10000:]);raise SystemExit(1)
flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-pthread',
 '-Itest/cloud_setup/runtime_stubs','-Itest/wifi_integration/stubs','-Itest/backup_integration','-Iinclude','-Ilib/wifi_setup_model','-Ilib/owner_cloud_setup','-Ilib/owner_login','-I'+str(OUT),'-isystem',str(JSON),'-isystem',str(DEPS/'include'),'-include','test/cloud_setup/runtime_stubs.h']
defines=['ARDUINOJSON_POOL_CAPACITY=128','CSP_ENABLE_OWNER_LOGIN=1','CSP_ENABLE_OWNER_CLOUD_SETUP=1','CSP_ENABLE_FILE_HISTORY=1','CSP_ENABLE_BACKUP_SETUP=1','CSP_ENABLE_WIFI_SETUP=1','CSP_ENABLE_BLE_MANAGEMENT=1','CSP_USB_BENCH_AUTOMATION=1','CONFIG_BT_NIMBLE_ENABLED=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
if INTEGRATED:
 flags[-1]='test/owner_login/integrated_stubs.h'
 defines+=['CSP_BACKUP_WIFI_AWAKE=1','CSP_PRODUCT_RC_PROFILE=1','CSP_PRODUCT_RC3_PROFILE=1',
  'CSP_PRODUCT_SPEED_TRIAL=1','CSP_UPLOAD_THROUGHPUT_TRIAL=1','CSP_PRODUCT_DASHBOARD_TRIAL=1',
  'CSP_ENABLE_LOCAL_DASHBOARD=1','CSP_UPLOAD_TX_BYTES=1460','CSP_ENABLE_SCAN_IDLE_GATE=1',
  'CSP_ENABLE_LCD_V2=1','CSP_DASHBOARD_PASSIVE_CLOSE=1']
binary=str(OUT/'runtime');publish()
run('build',[str(DEPS/'lib/llvm-18/bin/clang++'),*flags,*['-D'+d for d in defines],*sources,'-L'+str(DEPS/'lib/x86_64-linux-gnu'),'-lmbedx509','-lmbedcrypto','-o',binary])
for scenario in ['normal','wifi-busy','cloud-busy','abort','disconnect-stage','crc','mac','no-consent','stale-edit','window-expiry','cas','unknown','cleanup-fail','reload-fail','no-ack','disconnect-commit','late-job']:
 run(scenario,[binary,str(old),str(new),scenario])
if not publish(True):raise SystemExit('Source changed during run')
