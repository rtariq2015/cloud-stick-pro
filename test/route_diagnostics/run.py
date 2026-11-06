"""Actual adapter and UART diagnosis; no physical device, network or settings."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/route-diagnostics';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr');CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
paths=['src/owned_guest_route.cpp','include/owned_guest_route.h','include/route_diagnostics.h','src/wifi_setup_runtime.cpp',*[str(p.relative_to(ROOT)) for p in (ROOT/'test/route_diagnostics').glob('*') if p.is_file()]]
def hashes():return {p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
before=hashes();records=[]
def run(name,command,rejection=None):
 p=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=120)
 output=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(output)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
 passed=(p.returncode!=0 and rejection in output and not diagnostic) if rejection else p.returncode==0 and not p.stderr and not diagnostic
 records.append({'name':name,'exit':p.returncode,'passed':passed,'expected_compile_rejection':bool(rejection),'sanitizer_diagnostic':diagnostic,'log_sha256':hashlib.sha256(output.encode()).hexdigest()})
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:raise RuntimeError(output)
flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
enabled=['-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-DCSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_USB_BENCH_AUTOMATION=1']
binary=OUT/'route'
run('adapter-build',[CLANG,*flags,*enabled,'-DCSP_PRODUCT_LAB6_PROFILE=1','-Itest/owned_guest_route','-Itest/owned_guest_route/stubs','-Iinclude','src/owned_guest_route.cpp','test/route_diagnostics/route.test.cpp','-o',binary])
for scenario in ['late-start','down','stop']:run('adapter-'+scenario,[binary,scenario])
prefix=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text().split('\nint main(',1)[0]+'\n'
(OUT/'runtime_fixture_prefix.h').write_text(prefix)
prefix=(ROOT/'test/public_wifi_assistance/runtime.test.cpp').read_text().split('\nint main(',1)[0]+'\n'
(OUT/'public_runtime_prefix.h').write_text(prefix)
sys.path.insert(0,str(ROOT/'tools'));from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
fixture=OUT/'synthetic-owner.bin';fixture.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
runtime=['-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),'-Iinclude','-Ilib/wifi_setup_model','-isystem',str(JSON),'-include','test/backup_integration/runtime_stubs.h','-pthread','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
sources=['test/route_diagnostics/runtime.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp','src/public_wifi_assistance.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp']
for active in [0,1]:
 target=OUT/('runtime-'+str(active))
 run('runtime-build-'+str(active),[CLANG,*flags,*enabled,*runtime,'-DCSP_PRODUCT_LAB6_PROFILE='+str(active),*sources,'-o',target])
 run('runtime-UART-'+str(active),[target,fixture])
policy=OUT/'policy.cpp';policy.write_text('#include "route_diagnostics.h"\n')
for name,defines,error in [('invalid',['-DCSP_PRODUCT_LAB6_PROFILE=2'],'must be zero or one'),('nonbench',['-DCSP_PRODUCT_LAB6_PROFILE=1'],'explicit public routing USB bench')]:
 run('policy-'+name,[CLANG,'-std=c++17','-Iinclude',*defines,'-fsyntax-only',policy],rejection=error)
assert hashes()==before
(OUT/'results.json').write_text(json.dumps({'passed':True,'desktop_only':True,'device_contact':False,'settings_mutated':False,'source_stable':True,'source_sha256':before,'existing_startup_failure_observed_not_fixed':True,'hardware_failure_cause_not_assumed':True,'records':records},indent=2)+'\n')
