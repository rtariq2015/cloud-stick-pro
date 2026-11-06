"""Actual owner-loop/dashboard boundary regression; synthetic hardware only."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from pinned_test_dependencies import arduino_json_include
from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD

OUT=ROOT/'artifacts/local-dashboard-runtime'
DEPENDENCY_ROOT=Path(os.environ.get('CSP_DASHBOARD_TEST_DEPENDENCY_ROOT',ROOT))
JSON_INCLUDE=arduino_json_include(DEPENDENCY_ROOT)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1:detect_stack_use_after_return=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
         LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
OUT.mkdir(parents=True,exist_ok=True)
baseline=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text()
assert baseline.count('\nint main(')==1
fixture_prefix=baseline.split('\nint main(',1)[0]
(OUT/'runtime-fixture.inc').write_text(fixture_prefix)
fixture=OUT/'synthetic-owner.bin';fixture.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
inputs=['src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp','src/local_dashboard_model.cpp',
        'include/local_dashboard.h','include/pro_ble_lifecycle.h',
        'test/local_dashboard/runtime_stubs.h','test/local_dashboard/runtime.test.cpp',
        'test/local_dashboard/runtime.test.py','test/backup_integration/runtime_stubs.h',
        'test/backup_integration/runtime_handoff.test.cpp','test/wifi_integration/stubs/common.h']
hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
flags=['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
       '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-pthread','-Iinclude',
       '-Ilib/wifi_setup_model','-Itest/wifi_integration/stubs','-Itest/backup_integration','-isystem',str(JSON_INCLUDE),
       '-include','test/local_dashboard/runtime_stubs.h']
defines=['CSP_ENABLE_LOCAL_DASHBOARD=1','CSP_PRODUCT_DASHBOARD_TRIAL=1',
         'CSP_ENABLE_BLE_MANAGEMENT=1','CSP_ENABLE_WIFI_SETUP=1','CSP_ENABLE_BACKUP_SETUP=1',
         'CSP_USB_BENCH_AUTOMATION=1','CONFIG_BT_NIMBLE_ENABLED=1',
         'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
sources=['test/local_dashboard/runtime.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
         'src/local_dashboard_model.cpp','lib/wifi_setup_model/wifi_setup_model.cpp',
         'lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp',
         'lib/wifi_setup_model/owner_record.cpp']
records=[]
def publish(complete=False):
    stable=all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in hashes.items())
    passed=complete and stable and all(r['passed'] for r in records)
    (OUT/'results.json').write_text(json.dumps(dict(passed=passed,complete=complete,source_stable=stable,
       actual_source='src/wifi_setup_runtime.cpp',desktop_only=True,physical_device=False,
       arduino_json_include=str(JSON_INCLUDE),
       simulated=['USB','radio','heap','BLE','worker','NVS','dashboard socket boundary'],
       source_sha256=hashes,fixture_prefix_sha256=hashlib.sha256(fixture_prefix.encode()).hexdigest(),records=records),indent=2)+'\n')
    return passed
def run(name,command):
    p=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
    output=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(output)
    diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    passed=p.returncode==0 and not p.stderr and not diagnostic
    records.append(dict(name=name,command=command,exit=p.returncode,sanitizer_diagnostic=diagnostic,passed=passed))
    publish()
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:print(output[-10000:]);raise SystemExit(1)
for passive in (0,1):
    binary=str(OUT/('runtime-tests-'+str(passive)))
    run('build-'+str(passive),[str(DEPS/'lib/llvm-18/bin/clang++'),*flags,*['-D'+d for d in defines],
        '-DCSP_DASHBOARD_PASSIVE_CLOSE='+str(passive),*sources,'-o',binary])
    for scenario in ['normal-close','uncertain-close','snapshot-only','config-invalid']:
        run(scenario+'-'+str(passive),[binary,str(fixture),scenario])
if not publish(complete=True):
    raise SystemExit('Source changed during tests; final result is FAIL and requires a stable rerun')
