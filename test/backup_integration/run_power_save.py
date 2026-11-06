"""Strict synthetic tests of real runtime UART power-save bench controls. No device."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/power-save-bench'
DEPS = Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG = DEPS / 'lib/llvm-18/bin/clang++'
JSON = Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1:detect_stack_use_after_return=1',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1', LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT/'tools'))
    from create_wifi_owner_card import create_record, SYNTHETIC_PASSWORD
    fixture = OUT/'synthetic-owner.bin'
    fixture.write_bytes(create_record('001122334455', SYNTHETIC_PASSWORD))
    source = (ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text()
    assert source.count('\nint main(') == 1
    (OUT/'runtime_fixture_prefix.h').write_text(source.split('\nint main(')[0]+'\n')
    flags = ['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-pthread','-Iinclude',
             '-Ilib/wifi_setup_model',
             '-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),
             '-isystem',str(JSON),'-include','test/backup_integration/runtime_stubs.h']
    defines = ['CSP_ENABLE_BLE_MANAGEMENT=1','CSP_ENABLE_WIFI_SETUP=1','CSP_ENABLE_BACKUP_SETUP=1',
               'CONFIG_BT_NIMBLE_ENABLED=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1',
               'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
    sources = ['test/backup_integration/power_save.test.cpp','src/wifi_setup_nvs.cpp',
               'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp',
               'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp']
    inputs = ['src/wifi_setup_runtime.cpp','test/backup_integration/power_save.test.cpp',
              'test/wifi_integration/stubs/esp_wifi.h',
              'test/backup_integration/run_power_save.py','test/backup_integration/runtime_handoff.test.cpp']
    hashes = {name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs}
    records = []
    def run(name, args):
        result = subprocess.run(args,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
        output = result.stdout+result.stderr
        (OUT/(name+'.log')).write_text(output)
        diagnostic = bool(re.search(r'runtime error:|AddressSanitizer:|LeakSanitizer:|UndefinedBehaviorSanitizer:',output))
        passed = result.returncode == 0 and not result.stderr and not diagnostic
        records.append(dict(name=name,command=args,exit=result.returncode,stderr_empty=not result.stderr,
                            sanitizer_diagnostic=diagnostic,passed=passed))
        (OUT/'results.json').write_text(json.dumps(dict(desktop_only=True,device_access=False,
            actual_source='src/wifi_setup_runtime.cpp',simulated=['Wi-Fi SDK','NVS','BLE','USB','worker','heap'],
            source_sha256=hashes,records=records,passed=all(item['passed'] for item in records)),indent=2)+'\n')
        print(('PASS ' if passed else 'FAIL ')+name,flush=True)
        if not passed: print(output[-5000:],file=sys.stderr);raise SystemExit(1)
        return result.stdout
    for bench in [1,0]:
        binary = OUT/('runtime-bench-'+str(bench))
        run('build-bench-'+str(bench),[str(CLANG),*flags,*['-D'+item for item in defines],
            '-DCSP_USB_BENCH_AUTOMATION='+str(bench),*sources,'-o',str(binary)])
        run('runtime-bench-'+str(bench),[str(binary),str(fixture)])
    # Inspect actual runtime object, not the test executable which defines SDK stubs.
    for name, features in [('production-default',[]),('combined-bench-disabled',defines)]:
        obj = OUT/(name+'.o')
        run(name+'-build',[str(CLANG),*flags,*['-D'+item for item in features],
            '-c','src/wifi_setup_runtime.cpp','-o',str(obj)])
        symbols = run(name+'-symbols',['nm','-u',str(obj)])
        assert 'esp_wifi_set_ps' not in symbols and 'esp_wifi_get_ps' not in symbols
        assert b'POWER_SAVE_' not in obj.read_bytes()
    assert all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==value for name,value in hashes.items())
    print('PASS all source hashes unchanged during tests; no hardware or network used',flush=True)

if __name__ == '__main__': main()
