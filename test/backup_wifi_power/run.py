"""Actual managed owner-loop power policy; no device/cloud/network access."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from pinned_test_dependencies import arduino_json_include
from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
OUT=ROOT/'artifacts/backup-wifi-power';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1:detect_stack_use_after_return=1',
 UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
baseline=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text();assert baseline.count('\nint main(')==1
prefix=baseline.split('\nint main(',1)[0];(OUT/'runtime-fixture-prefix.h').write_text(prefix+'\n')
fixture=OUT/'synthetic-owner.bin';fixture.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
sources=['src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp','include/backup_wifi_power.h',
 'include/backup_worker.h','include/pro_ble_lifecycle.h','test/backup_integration/runtime_handoff.test.cpp',
 'test/backup_integration/runtime_stubs.h','test/wifi_integration/stubs/common.h','test/wifi_integration/stubs/esp_wifi.h',
 'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp',
 'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp',
 *[p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'test/backup_wifi_power').rglob('*')) if p.is_file() and p.suffix in {'.h','.cpp','.py'}]]
hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}
flags=['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
 '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-pthread','-Iinclude',
 '-Itest/backup_wifi_power/stubs','-Itest/wifi_integration/stubs',
 '-Itest/backup_integration','-Ilib/wifi_setup_model','-I'+str(OUT),'-isystem',str(arduino_json_include(ROOT))]
defines=['CSP_BACKUP_WIFI_AWAKE=1','CSP_PRODUCT_RC_PROFILE=1','CSP_PRODUCT_RC2_PROFILE=1',
 'CSP_ENABLE_BACKUP_SETUP=1','CSP_ENABLE_WIFI_SETUP=1','CSP_ENABLE_BLE_MANAGEMENT=1',
 'CSP_USB_BENCH_AUTOMATION=1','CONFIG_BT_NIMBLE_ENABLED=1',
 'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
records=[]
def publish(complete=False):
 stable=all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in hashes.items())
 passed=complete and stable and all(r['passed'] for r in records)
 (OUT/'results.json').write_text(json.dumps(dict(passed=passed,complete=complete,source_stable=stable,
  desktop_only=True,device_access=False,actual_source='src/wifi_setup_runtime.cpp',
  simulated=['Wi-Fi SDK','worker','BLE','USB','NVS','heap'],source_sha256=hashes,
  shared_fixture_prefix_sha256=hashlib.sha256(prefix.encode()).hexdigest(),records=records),indent=2)+'\n')
 return passed
def run(name,command):
 p=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
 text=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 passed=p.returncode==0 and not p.stderr and not diagnostic
 records.append(dict(name=name,command=command,exit=p.returncode,passed=passed,sanitizer_diagnostic=diagnostic))
 publish();print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:print(text[-10000:]);raise SystemExit(1)
clang=str(DEPS/'lib/llvm-18/bin/clang++')
model=str(OUT/'model-tests');run('model-build',[clang,*flags,'test/backup_wifi_power/model.test.cpp','-o',model]);run('model',[model])
binary=str(OUT/'runtime-tests')
run('runtime-build',[clang,*flags,*['-D'+d for d in defines],'-include','test/backup_wifi_power/runtime_stubs.h',
 'test/backup_wifi_power/runtime.test.cpp','src/wifi_setup_nvs.cpp',
 'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp',
 'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp','-o',binary])
for scenario in ['normal-min','normal-max','normal-none','capture-read-fail','awake-set-fail','awake-read-fail',
                 'awake-mismatch','invalid-mode','worker-start-fail','manual-control','stale-retirement',
                 'restore-set-fail','restore-read-fail','restore-mismatch','fresh-driver-cycle','policy-status']:
 run(scenario,[binary,str(fixture),scenario])
if not publish(complete=True):raise SystemExit('FAIL:source changed; stable rerun required')
