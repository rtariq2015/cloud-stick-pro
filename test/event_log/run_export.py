#!/usr/bin/env python3
"""Strict real runtime/wire event export tests; synthetic files, no hardware/network."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/event-log-export'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
FLAGS=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
       '-Iinclude','-Ilib/wifi_setup_model','-isystem',str(JSON),'-pthread']
RECORD=[]
def run(name,command):
    result=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
    output=result.stdout+result.stderr;log=OUT/(name+'.log');log.write_text(output)
    diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    passed=result.returncode==0 and not result.stderr and not diagnostic
    RECORD.append(dict(name=name,command=list(map(str,command)),exit=result.returncode,stderr_empty=not result.stderr,
                       sanitizer_diagnostic=diagnostic,passed=passed,log=str(log.relative_to(ROOT)),
                       log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
    (OUT/'results.json').write_text(json.dumps(dict(passed=all(r['passed'] for r in RECORD),desktop_only=True,records=RECORD),indent=2)+'\n')
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:raise RuntimeError(output)
    return result.stdout

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    inputs=['src/wifi_setup_runtime.cpp','src/pro_event_log.cpp','src/ble_management.cpp','include/pro_event_log.h',
            'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/wifi_wire.h',
            'test/backup_integration/runtime_handoff.test.cpp','test/public_wifi_assistance/runtime.test.cpp',
            'test/wifi_integration/stubs/esp_wifi.h','test/event_log/io.h',
            'test/event_log/export_wire.test.cpp','test/event_log/export_runtime.test.cpp',
            'test/event_log/header_off.test.cpp','test/event_log/run_export.py']
    before={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
    for mode,defines in [
        ('default',[]),('off',['-DCSP_ENABLE_EVENT_LOG=0','-DCSP_ENABLE_BACKUP_SETUP=1']),
        ('event-without-backup',['-DCSP_ENABLE_EVENT_LOG=1','-DCSP_ENABLE_BACKUP_SETUP=0']),
        ('backup-without-event',['-DCSP_ENABLE_BACKUP_SETUP=1']),
        ('enabled',['-DCSP_ENABLE_EVENT_LOG=1','-DCSP_ENABLE_BACKUP_SETUP=1'])]:
        binary=OUT/('wire-'+mode)
        run('wire-'+mode+'-build',[CLANG,*FLAGS,*defines,'lib/wifi_setup_model/wifi_wire.cpp',
            'lib/wifi_setup_model/wifi_setup_model.cpp','test/event_log/export_wire.test.cpp','-o',binary])
        run('wire-'+mode,[binary])
    # Reproduce GCC's default-off aggregate header handling as well as Clang's.
    run('gcc-default-off-header-build',['g++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude',
                                      'test/event_log/header_off.test.cpp','-o',OUT/'header-off'])
    run('gcc-default-off-header',[OUT/'header-off'])
    import sys
    sys.path.insert(0,str(ROOT/'tools'))
    from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
    fixture=OUT/'synthetic-owner.bin';fixture.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
    for source,target in [('test/backup_integration/runtime_handoff.test.cpp','runtime_fixture_prefix.h'),
                          ('test/public_wifi_assistance/runtime.test.cpp','public_runtime_fixture_prefix.h')]:
        text=(ROOT/source).read_text();assert text.count('\nint main(')==1
        (OUT/target).write_text(text.split('\nint main(',1)[0]+'\n')
    runtime_flags=['-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),
                   '-include','test/backup_integration/runtime_stubs.h','-include','test/event_log/io.h',
                   '-DCSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-DCSP_ENABLE_BACKUP_SETUP=1',
                   '-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_USB_BENCH_AUTOMATION=1',
                   '-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1',
                   '-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1','-DCSP_EVENT_LOG_TESTING=1']
    total_cases=0;maximum_plaintext=maximum_envelope=0
    with tempfile.TemporaryDirectory(prefix='csp-event-export-') as tmp:
        for enabled in [0,1]:
            binary=OUT/('runtime-'+str(enabled))
            run('runtime-'+str(enabled)+'-build',[CLANG,*FLAGS,*runtime_flags,
                '-DCSP_ENABLE_EVENT_LOG='+str(enabled),'-DCSP_EVENT_LOG_ROOT="'+tmp+'/events"',
                'test/event_log/export_runtime.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
                'src/public_wifi_assistance.cpp','src/pro_event_log.cpp','lib/wifi_setup_model/wifi_setup_model.cpp',
                'lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp',
                'lib/wifi_setup_model/owner_record.cpp','-o',binary])
            output=run('runtime-'+str(enabled),[binary,fixture])
            total_cases+=int(re.search(r'(\d+) actual export runtime cases passed',output)[1])
            if enabled:
                sizes=re.search(r'maximum_plaintext_bytes=(\d+) maximum_envelope_bytes=(\d+)',output)
                assert sizes
                maximum_plaintext,maximum_envelope=map(int,sizes.groups())
                assert maximum_plaintext<400 and maximum_envelope<496
    after={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in inputs}
    if before!=after:
        (OUT/'results.json').write_text(json.dumps(dict(passed=False,source_stable=False,records=RECORD),indent=2)+'\n')
        raise RuntimeError('Reviewed source changed during export suite')
    result=dict(passed=True,desktop_only=True,device_contact=False,cloud_contact=False,real_sd_operations=False,
                strict_asan_ubsan=True,leak_detection=True,wire_variants=5,runtime_cases=total_cases,
                maximum_plaintext_bytes=maximum_plaintext,maximum_envelope_bytes=maximum_envelope,
                source_stable=True,source_sha256=after,records=RECORD)
    (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS strict actual event export runtime, five wire variants and GCC default-off header',flush=True)
if __name__=='__main__':main()
