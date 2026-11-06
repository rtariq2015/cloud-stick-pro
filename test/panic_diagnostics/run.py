"""Strict native tests of the actual lab panic recorder; no physical panic."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/panic-diagnostics';OUT.mkdir(parents=True,exist_ok=True)
SDK=Path('/config/.unmanic/research/cloud-stick-sdk-memory-core-20261104-asym-iram/packages/framework-arduinoespressif32')
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
paths=['include/panic_diagnostics.h','src/panic_diagnostics.cpp','src/wifi_setup_runtime.cpp','platformio.ini','test/panic_diagnostics/panic.test.cpp','test/panic_diagnostics/runtime.test.cpp','test/panic_diagnostics/run.py','test/panic_diagnostics/stubs/esp_attr.h','test/panic_diagnostics/stubs/esp_system.h','test/panic_diagnostics/stubs/esp32-hal.h']
def hashes():return {p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
before=hashes();records=[]
def run(name,command,expected_rejection=None):
 p=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=90)
 text=p.stdout+p.stderr;log=OUT/(name+'.log');log.write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 passed=(p.returncode!=0 and expected_rejection in text and not diagnostic) if expected_rejection else (p.returncode==0 and not p.stderr and not diagnostic)
 records.append(dict(name=name,command=list(map(str,command)),exit=p.returncode,passed=passed,expected_compile_rejection=bool(expected_rejection),sanitizer_diagnostic=diagnostic,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:raise RuntimeError(text)
 return p.stdout
header=(SDK/'cores/esp32/esp32-hal.h').read_text();core=(SDK/'cores/esp32/esp32-hal-misc.c').read_text()
abi=re.search(r'typedef struct \{[^{}]*\} arduino_panic_info_t;',header).group(0)
handler=re.search(r'typedef void \(\*arduino_panic_handler_t\)\(arduino_panic_info_t \*info, void \*arg\);',header).group(0)
assert 'unsigned int backtrace[60]' in abi
(OUT/'pinned_panic_abi.h').write_text('#pragma once\n'+abi+'\n'+handler+'\n')
start=core.index('void __wrap_esp_panic_handler(panic_info_t *info) {');end=core.index('\n}',start)+2
wrapper=core[start:end];assert wrapper.count('__real_esp_panic_handler(info);')==1
(OUT/'pinned_wrapper.h').write_text(wrapper+'\n')
flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Iinclude','-Itest/panic_diagnostics/stubs','-I'+str(OUT)]
run('enabled-build',[CLANG,*flags,'-DCSP_PANIC_DIAGNOSTICS=1','-DCSP_PRODUCT_LAB4_PROFILE=1','-DCONFIG_IDF_TARGET_ESP32S3=1','test/panic_diagnostics/panic.test.cpp','-o',OUT/'recorder'])
cases=['maximum','empty','sixty','oversized','continues','corrupt-trace','invalid-core','negative-core','null','bad-magic','partial','bad-checksum','bad-version','bad-count','bad-core','bad-flags','power-on','software-reset','watchdog-reset','idempotent','freshness','format-bounds']
keys={'panic_valid','reset_reason','core','pc','count','backtrace_corrupt','backtrace_truncated','pcs'}
for case in cases:
 d=json.loads(run(case,[OUT/'recorder',case]));assert set(d)==keys
 assert all(type(d[k]) is bool for k in ['panic_valid','backtrace_corrupt','backtrace_truncated'])
 assert all(type(d[k]) is int and 0<=d[k]<=0xffffffff for k in ['reset_reason','core','pc','count'])
 assert type(d['pcs']) is list and len(d['pcs'])==d['count']<=16 and all(type(v) is int and 0<=v<=0xffffffff for v in d['pcs'])
 assert d['panic_valid'] or (not d['pcs'] and not d['count'] and not d['pc'])
 if case in ['sixty','oversized','continues']:assert d['backtrace_truncated']
 if case=='corrupt-trace':assert d['backtrace_corrupt']
off_flags=[flag for flag in flags if not flag.startswith('-fsanitize=')]
run('default-off-build',[CLANG,*off_flags,'-c','src/panic_diagnostics.cpp','-o',OUT/'disabled.o'])
symbols=run('default-off-symbols',['nm','--defined-only',OUT/'disabled.o']);assert not symbols.strip()
run('reject-nonlab',[CLANG,*off_flags,'-DCSP_PANIC_DIAGNOSTICS=1','-DCONFIG_IDF_TARGET_ESP32S3=1','-c','src/panic_diagnostics.cpp','-o',OUT/'rejected-nonlab.o'],expected_rejection='Panic diagnostics are restricted to the explicit lab4 profile')
run('reject-invalid-flag',[CLANG,*off_flags,'-DCSP_PANIC_DIAGNOSTICS=2','-DCSP_PRODUCT_LAB4_PROFILE=1','-DCONFIG_IDF_TARGET_ESP32S3=1','-c','src/panic_diagnostics.cpp','-o',OUT/'rejected-flag.o'],expected_rejection='CSP_PANIC_DIAGNOSTICS must be zero or one')
prefix=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text();assert prefix.count('\nint main(')==1
(OUT/'runtime_fixture_prefix.h').write_text(prefix.split('\nint main(',1)[0]+'\n')
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
runtime_flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
 '-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),'-Iinclude','-Ilib/wifi_setup_model','-isystem',str(JSON),'-include','test/backup_integration/runtime_stubs.h','-pthread',
 '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_USB_BENCH_AUTOMATION=1',
 '-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
sources=['test/panic_diagnostics/runtime.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp']
for enabled in [0,1]:
 run('runtime-build-'+str(enabled),[CLANG,*runtime_flags,'-DCSP_PANIC_DIAGNOSTICS='+str(enabled),'-DCSP_PRODUCT_LAB4_PROFILE='+str(enabled),*sources,'-o',OUT/('runtime-'+str(enabled))])
 run('runtime-UART-'+str(enabled),[OUT/('runtime-'+str(enabled))])
config=(ROOT/'platformio.ini').read_text();assert config.count('-DCSP_PANIC_DIAGNOSTICS=1')==1
assert config.count('-Wl,--wrap=esp_panic_handler')==1
assert config.index('-DCSP_PANIC_DIAGNOSTICS=1')>config.index('[env:tdongle_s3_product_lab4]')
runtime=(ROOT/'src/wifi_setup_runtime.cpp').read_text();assert runtime.index('panic_diagnostics_begin();')<runtime.index('delay(500);pinMode(0')
assert hashes()==before
result=dict(passed=True,desktop_only=True,device_contact=False,real_panic=False,target_IRAM_proof_pending=True,callback_cases=len(cases),source_stable=True,source_sha256=before,pinned_sources={str(SDK/p):hashlib.sha256((SDK/p).read_bytes()).hexdigest() for p in ['cores/esp32/esp32-hal.h','cores/esp32/esp32-hal-misc.c']},wrapper_sha256=hashlib.sha256(wrapper.encode()).hexdigest(),records=records)
(OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: actual recorder and exact pinned wrapper body; native walker is synthetic; target placement pending')
