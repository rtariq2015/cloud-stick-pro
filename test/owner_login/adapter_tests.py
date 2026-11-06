"""Actual BLE adapter ACK/output-barrier tests, with explicitly fake SDK crypto."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/owner-login-adapter';OUT.mkdir(parents=True,exist_ok=True)
if (OUT/'results.json').exists():
 attempt=len(list(OUT.glob('attempt-*.json')))+1;(OUT/f'attempt-{attempt}.json').write_bytes((OUT/'results.json').read_bytes())
 for old in OUT.glob('*.log'):
  if not old.name.startswith('attempt-'):(OUT/f'attempt-{attempt}-{old.name}').write_bytes(old.read_bytes())
sys.path.insert(0,str(ROOT/'tools'));from pinned_test_dependencies import arduino_json_include
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr');JSON=arduino_json_include(ROOT)
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
inputs=['test/owner_login/adapter.cpp','test/owner_login/adapter_tests.py','test/owner_login/adapter_stubs/esp_err.h','src/ble_management.cpp','include/wifi_setup_runtime.h','include/owner_login_feature.h','lib/wifi_setup_model/wifi_wire.h','lib/wifi_setup_model/wifi_wire.cpp','lib/owner_login/owner_login_model.h']
inputs+=['include/ble_feature.h','include/ble_management.h','include/pro_ble_message.h',
 'test/milestone1_corrections/adapter_timer_interleavings.cpp','test/milestone1_corrections/adapter_callback_boundaries.cpp']
inputs += [str(p) for p in (ROOT/'test/milestone1_corrections/adapter_stubs').glob('*.h')]
inputs=[str(Path(p).relative_to(ROOT))if Path(p).is_absolute()else p for p in inputs]
hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest()for f in inputs};records=[]
def publish(final=False):
 stable=all(hashlib.sha256((ROOT/f).read_bytes()).hexdigest()==h for f,h in hashes.items());(OUT/'results.json').write_text(json.dumps(dict(passed=final and stable and all(r['passed']for r in records),complete=final,source_stable=stable,physical=False,real_crypto=False,source_sha256=hashes,records=records),indent=2)+'\n');return stable
def run(name,cmd):
 p=subprocess.run(cmd,cwd=ROOT,env=ENV,text=True,capture_output=True,timeout=180);text=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(text);diag=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text));ok=p.returncode==0 and not p.stderr and not diag;records.append(dict(name=name,passed=ok,exit=p.returncode,command=cmd,sanitizer_diagnostic=diag));publish();print(name,'PASS'if ok else'FAIL',flush=True)
 if not ok:print(text[-12000:]);raise SystemExit(1)
binary=str(OUT/'adapter');publish()
run('build',[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Itest/owner_login/adapter_stubs','-Itest/milestone1_corrections/adapter_stubs','-Iinclude','-Ilib/wifi_setup_model','-Ilib/owner_login','-isystem',str(JSON),'-DCSP_ENABLE_OWNER_LOGIN=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1','test/owner_login/adapter.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','-o',binary])
for scenario in ['normal','late-info','lease','malformed']:run(scenario,[binary,scenario])
for source,cases in [
 ('adapter_timer_interleavings.cpp',['stale-connect','future-connect','future-info','timeout-wrap','timeout-new-generation','timeout-refreshed-generation','absolute-window']),
 ('adapter_callback_boundaries.cpp',['new-session','handshake','decrypt','encrypt','info','replaced-session'])]:
 old=str(OUT/('default-'+source[:-4]));name=source[:-4]
 run('default-build-'+name,[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Itest/milestone1_corrections/adapter_stubs','-Iinclude','-DCSP_ENABLE_OWNER_LOGIN=0','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_BLE_SYNTHETIC_ONLY=1','-DCSP_BLE_ISOLATED_TEST=1','-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1',str(ROOT/'test/milestone1_corrections'/source),'-o',old])
 for case in cases:run('default-'+case,[old,case])
if not publish(True):raise SystemExit('Source changed during run')
