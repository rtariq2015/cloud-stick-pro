"""Strict native actual codec/NVS tests. Synthetic values and public CA only."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/owner-cloud-store';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
LIB=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson')
assert json.loads((LIB/'library.json').read_text())['version']=='7.4.3'
assert '#define ARDUINOJSON_VERSION "7.4.3"' in (LIB/'src/ArduinoJson/version.hpp').read_text()
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
files=['lib/owner_cloud_setup/owner_cloud_store.cpp','lib/owner_cloud_setup/owner_cloud_store.h',
 'lib/owner_cloud_setup/owner_cloud_model.cpp','lib/owner_cloud_setup/owner_cloud_model.h',
 'include/owner_cloud_nvs.h','src/owner_cloud_nvs.cpp','include/owner_cloud_feature.h',
 'include/config_loader.h','src/config_loader.cpp','include/koofr_ca.h','src/pro_core.cpp',
 'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_setup_model.h',
 'lib/wifi_setup_model/wifi_profile_store.h','lib/wifi_setup_model/secure_json.h',
 'test/backup_integration/config_loader.test.cpp','test/backup_integration/config_stubs/Arduino.h',
 'test/backup_integration/config_stubs/nvs.h','test/backup_integration/config_stubs/Preferences.h',
 'test/cloud_setup/store.test.cpp','test/cloud_setup/store_stubs/nvs.h','test/cloud_setup/store_stubs/Preferences.h','test/cloud_setup/store_tests.py']
hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in files}
records=[]
def publish(done=False):
 stable=all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in hashes.items())
 result=dict(passed=done and stable and all(r['passed'] for r in records),complete=done,source_stable=stable,
  desktop_only=True,device_access=False,network_access=False,real_credentials=False,actual_codec=True,actual_nvs_adapter=True,
  source_sha256=hashes,arduino_json='7.4.3',target_like_native_pool_slots=128,default_native_pool_slots=256,
  target_pool_bytes=1024,target_like_native_pool_bytes=2048,default_native_pool_bytes=4096,parser_budget_bytes=8192,
  actual_worker_and_legacy_loader_roundtrip=True,records=records)
 (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n');return result['passed']
def run(name,cmd):
 p=subprocess.run(cmd,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
 text=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 secrets_absent=all(secret not in text for secret in ['synthetic-private-app-password','synthetic-saved-password','legacy-secret-must-stay','synthetic-wifi-password'])
 passed=p.returncode==0 and not p.stderr and not diagnostic and secrets_absent
 records.append(dict(name=name,command=cmd,exit=p.returncode,passed=passed,sanitizer_diagnostic=diagnostic,synthetic_secrets_absent=secrets_absent));publish()
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:print(text[-8000:]);raise SystemExit(1)
binary=str(OUT/'store-tests')
cmd=[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',
 '-DARDUINOJSON_POOL_CAPACITY=128',
 '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
 '-DCSP_ENABLE_OWNER_CLOUD_SETUP=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1',
 '-Itest/cloud_setup/store_stubs','-Itest/backup_integration/config_stubs','-Iinclude','-Ilib/owner_cloud_setup','-Ilib/wifi_setup_model',
 '-isystem',str(LIB/'src'),'-I'+str(DEPS/'include'),
 'lib/owner_cloud_setup/owner_cloud_store.cpp','lib/owner_cloud_setup/owner_cloud_model.cpp',
 'src/owner_cloud_nvs.cpp','src/config_loader.cpp','src/pro_core.cpp','lib/wifi_setup_model/wifi_setup_model.cpp',
 'test/cloud_setup/store.test.cpp','-L'+str(DEPS/'lib/x86_64-linux-gnu'),'-lmbedx509','-lmbedcrypto','-o',binary]
publish();run('build',cmd);run('run',[binary])
default_cmd=[argument for argument in cmd if argument!='-DARDUINOJSON_POOL_CAPACITY=128']
default_cmd[-1]=binary+'-native-large-pool'
run('build-default-native',default_cmd);run('run-default-native',[default_cmd[-1],'--native-large-pool'])
legacy_cmd=[argument for argument in default_cmd if argument not in ['-DCSP_ENABLE_OWNER_CLOUD_SETUP=1',
 '-Itest/cloud_setup/store_stubs','-Ilib/owner_cloud_setup','lib/owner_cloud_setup/owner_cloud_store.cpp',
 'lib/owner_cloud_setup/owner_cloud_model.cpp','src/owner_cloud_nvs.cpp']]
legacy_cmd[legacy_cmd.index('test/cloud_setup/store.test.cpp')]='test/backup_integration/config_loader.test.cpp'
legacy_cmd[-1]=binary+'-existing-loader'
run('build-existing-loader-default-off',legacy_cmd)
run('run-existing-loader-default-off',[legacy_cmd[-1],'/etc/ssl/certs/ISRG_Root_X1.pem'])
if not publish(done=True):raise SystemExit('FAIL:source changed during tests; stable rerun required')
