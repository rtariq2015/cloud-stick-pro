"""Build/run source-only M2 checks. No serial, device, private key or cloud access."""
from pathlib import Path
from pinned_test_dependencies import arduino_json_include
import hashlib
import json
import os
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/os.environ.get('CSP_WIFI_TEST_OUT','artifacts/wifi-integration');OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
         LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
         LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),C_INCLUDE_PATH=str(DEPS/'include'))
CLANG=DEPS/'lib/llvm-18/bin/clang++'
JSON=arduino_json_include(ROOT)
commands=[]
def run(name,command,timeout=180):
    r=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=timeout)
    (OUT/(name+'.log')).write_text(r.stdout+r.stderr)
    commands.append({'name':name,'command':list(map(str,command)),'exit':r.returncode,'stderr_empty':not r.stderr})
    if r.returncode or r.stderr:raise RuntimeError(name+' failed; inspect '+str(OUT/(name+'.log')))
    print('PASS',name,flush=True)
    return r.stdout

def main():
    from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
    fixture=OUT/'synthetic-owner-record.bin';fixture.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
    flags=['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
           '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Ilib/wifi_setup_model','-Iinclude','-isystem',JSON]
    run('store-build',[CLANG,*flags,'lib/wifi_setup_model/wifi_setup_model.cpp',
        'lib/wifi_setup_model/wifi_profile_store.cpp','test/wifi_profile_store.test.cpp','-o',OUT/'store-tests'])
    run('store-tests',[OUT/'store-tests'])
    run('wire-build',[CLANG,*flags,'lib/wifi_setup_model/wifi_setup_model.cpp',
        'lib/wifi_setup_model/owner_record.cpp','lib/wifi_setup_model/wifi_wire.cpp',
        'test/wifi_integration/wire_owner.test.cpp','-o',OUT/'wire-tests'])
    run('wire-tests',[OUT/'wire-tests',fixture])
    defines=['-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCONFIG_BT_NIMBLE_ENABLED=1',
             '-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
    run('radio-build',[CLANG,*flags,*defines,'-Itest/wifi_integration/radio_stubs',
        'test/wifi_integration/radio.test.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','-o',OUT/'radio-tests'])
    run('radio-tests',[OUT/'radio-tests'])
    release_defines=[*defines,'-DCSP_USB_BENCH_AUTOMATION=1','-DCSP_BENCH_RADIO_RELEASE=1']
    run('radio-release-build',[CLANG,*flags,*release_defines,'-Itest/wifi_integration/radio_stubs',
        'test/wifi_integration/radio.test.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','-o',OUT/'radio-release-tests'])
    run('radio-release-tests',[OUT/'radio-release-tests'])
    run('enrollment-build',[CLANG,*flags,*defines,'-pthread','-Itest/wifi_integration/stubs',
        'test/wifi_integration/enrollment.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
        'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp',
        'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp','-o',OUT/'enrollment-tests'])
    for case in ['normal','held-short','media-present','host-media-unmounted','radio-busy','owner-corrupt','record-invalid','lost-ack','init-fail']:
        run('enrollment-'+case,[OUT/'enrollment-tests',fixture,case])
    run('bench-enrollment-build',[CLANG,*flags,*defines,'-DCSP_USB_BENCH_AUTOMATION=1','-pthread','-Itest/wifi_integration/stubs',
        'test/wifi_integration/enrollment.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
        'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp',
        'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp','-o',OUT/'bench-enrollment-tests'])
    for case in ['normal','held-short','media-present','host-media-unmounted','radio-busy','owner-corrupt','record-invalid','lost-ack','init-fail']:
        run('bench-enrollment-'+case,[OUT/'bench-enrollment-tests',fixture,case])
    run('radio-release-runtime-build',[CLANG,*flags,*release_defines,'-pthread','-Itest/wifi_integration/stubs',
        'test/wifi_integration/enrollment.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
        'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp',
        'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp','-o',OUT/'radio-release-runtime-tests'])
    for case in ['normal','held-short','media-present','host-media-unmounted','radio-busy','owner-corrupt','record-invalid','lost-ack','init-fail','release-fail','reinit-fail','ble-stop-fail','release-memory-fail']:
        run('radio-release-runtime-'+case,[OUT/'radio-release-runtime-tests',fixture,case])
    # Compile exact pinned Security 2, SRP and protobuf C with the same reviewed
    # byte-safe IV/RNG lifetime patches as the passing M1 baseline.
    run('sdk-provenance',[sys.executable,'tools/pinned_ble_sdk.py'])
    source=ROOT/'test/milestone1';sdk=Path(os.environ['PLATFORMIO_CORE_DIR'])/'packages/framework-arduinoespressif32-libs/esp32s3/include/protocomm'
    objects=[]
    for name in ['security2.c','esp_srp.c','esp_srp_mpi.c','session.pb-c.c','sec0.pb-c.c','sec1.pb-c.c','sec2.pb-c.c','constants.pb-c.c']:
        obj=OUT/(name+'.o');objects.append(obj)
        vendor=ROOT/'sdk/pinned_ble/patched/security2.c' if name=='security2.c' else source/'sdk_vendor'/name
        c=['gcc','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
           '-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-include',source/'host_shims/mbedtls_compat.h']
        for inc in [source/'host_shims',source/'sdk_vendor',sdk/'include/security',sdk/'include/crypto/srp6a',sdk/'proto-c',DEPS/'include']:c+=['-I',inc]
        run('sdk-'+name,[*c,'-c',vendor,'-o',obj])
    # Clang C++ O1 keeps strict warnings without the previously documented GCC
    # optimized ArduinoJson false-positive; all code remains sanitizer-instrumented.
    run('sdk-runtime-build',[CLANG,*flags,*defines,'-pthread','-Itest/wifi_integration/stubs','-I'+str(DEPS/'include'),
        'test/wifi_integration/sdk_runtime_host.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
        'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp',
        'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp',*objects,
        '-lmbedtls','-lmbedcrypto','-lmbedx509','-lprotobuf-c','-o',OUT/'sdk-runtime-host'])
    (OUT/'build-record.json').write_text(json.dumps({'commands':commands,'synthetic_owner':True,
        'owner_fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'scope':'Desktop: actual parser/policy/store/runtime/NVS adapter + SDK crypto; radio/USB/NVS hardware simulated',
        'device_contact':False},indent=2)+'\n')

if __name__=='__main__':main()
