"""Targeted actual route proof; native synthetic SDK adapters, no hardware."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/owned-guest-liveness';OUT.mkdir(parents=True,exist_ok=True)
BASE='8ebe8d758a8725ceae81482da59f57bcb238ac3c'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
FLAGS=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-Itest/owned_guest_route/stubs','-Iinclude']
paths=['src/owned_guest_route.cpp','src/wifi_setup_runtime.cpp','test/owned_guest_route/stubs/platform.h','test/owned_guest_route/ap_liveness.test.cpp','test/owned_guest_route/runtime_liveness.test.cpp']
before={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
records=[]
def run(name,command,old_failure=None):
 r=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=120)
 text=r.stdout+r.stderr;log=OUT/(name+'.log');log.write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 passed=(r.returncode!=0 and old_failure in text and not diagnostic) if old_failure else (r.returncode==0 and not r.stderr and not diagnostic)
 records.append(dict(name=name,command=list(map(str,command)),exit=r.returncode,passed=passed,expected_old_failure=bool(old_failure),sanitizer_diagnostic=diagnostic,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
 (OUT/'results.json').write_text(json.dumps(dict(passed=all(x['passed'] for x in records),records=records),indent=2)+'\n')
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:raise RuntimeError(text)
old=OUT/'old_route.cpp';old.write_bytes(subprocess.check_output(['git','show',BASE+':src/owned_guest_route.cpp'],cwd=ROOT))
run('baseline-build',[CLANG,*FLAGS,old,'test/owned_guest_route/ap_liveness.test.cpp','-o',OUT/'old-route'])
for mode in ['stop','netif']:run('baseline-'+mode,[OUT/'old-route',mode],old_failure='status().phase==Phase::Stopping')
run('fixed-build',[CLANG,*FLAGS,'src/owned_guest_route.cpp','test/owned_guest_route/ap_liveness.test.cpp','-o',OUT/'fixed-route'])
run('fixed-groups',[OUT/'fixed-route'])
for name,target in [('test/backup_integration/runtime_handoff.test.cpp','runtime_fixture_prefix.h'),('test/public_wifi_assistance/runtime.test.cpp','public_route_runtime_fixture.h')]:
 text=(ROOT/name).read_text();assert text.count('\nint main(')==1
 (OUT/target).write_text(text.split('\nint main(',1)[0]+'\n')
sys.path.insert(0,str(ROOT/'tools'));from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
owner=OUT/'synthetic-owner.bin';owner.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
runtime_flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
 '-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),'-Iinclude','-Ilib/wifi_setup_model','-isystem',str(JSON),'-include','test/backup_integration/runtime_stubs.h','-pthread',
 '-DCSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_USB_BENCH_AUTOMATION=1',
 '-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
sources=['test/owned_guest_route/runtime_liveness.test.cpp','src/wifi_setup_nvs.cpp','src/public_wifi_assistance.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp']
old_runtime=OUT/'old_runtime.cpp';old_runtime.write_bytes(subprocess.check_output(['git','show',BASE+':src/wifi_setup_runtime.cpp'],cwd=ROOT))
run('baseline-runtime-build',[CLANG,*runtime_flags,*sources,old_runtime,'-o',OUT/'old-runtime'])
run('baseline-runtime-ap-proof',[OUT/'old-runtime',owner,'ap-proof'],old_failure='Cleaned AP fault incorrectly accepted instead of blocked')
run('fixed-runtime-build',[CLANG,*runtime_flags,*sources,'src/wifi_setup_runtime.cpp','-o',OUT/'fixed-runtime'])
for scenario in ['ap-proof','socket','retained','expired','normal']:run('fixed-runtime-'+scenario,[OUT/'fixed-runtime',owner,scenario])
after={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths};assert before==after
(OUT/'results.json').write_text(json.dumps(dict(passed=True,desktop_only=True,device_contact=False,network_contact=False,baseline_commit=BASE,sources_unchanged=True,source_sha256=after,case_groups=7,actual_runtime_scenarios=5,records=records),indent=2)+'\n')
