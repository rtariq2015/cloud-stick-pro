"""Strict desktop tests of the actual combined owner loop; no device access."""
from pathlib import Path
from pinned_test_dependencies import arduino_json_include
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/backup-wifi-integration/runtime'
DEPS = Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG = Path(os.environ.get('CSP_TEST_CLANGXX', str(DEPS / 'lib/llvm-18/bin/clang++')))
JSON = arduino_json_include(ROOT)
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1:detect_stack_use_after_return=1',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
           LD_LIBRARY_PATH=str(DEPS / 'lib/x86_64-linux-gnu'))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    from create_wifi_owner_card import create_record, SYNTHETIC_PASSWORD
    fixture = OUT / 'synthetic-owner.bin'
    fixture.write_bytes(create_record('001122334455', SYNTHETIC_PASSWORD))
    flags = ['-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-fno-omit-frame-pointer', '-pthread', '-Iinclude',
             '-Ilib/wifi_setup_model', '-Itest/wifi_integration/stubs',
             '-isystem', str(JSON), '-include', 'test/backup_integration/runtime_stubs.h']
    defines = ['CSP_ENABLE_BLE_MANAGEMENT=1', 'CSP_ENABLE_WIFI_SETUP=1',
               'CSP_ENABLE_BACKUP_SETUP=1', 'CSP_USB_BENCH_AUTOMATION=1',
               'CONFIG_BT_NIMBLE_ENABLED=1',
               'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1',
               'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
    command = [str(CLANG), *flags, *['-D' + item for item in defines],
               'test/backup_integration/runtime_handoff.test.cpp',
               'src/wifi_setup_runtime.cpp', 'src/wifi_setup_nvs.cpp',
               'lib/wifi_setup_model/wifi_setup_model.cpp',
               'lib/wifi_setup_model/wifi_profile_store.cpp',
               'lib/wifi_setup_model/wifi_wire.cpp',
               'lib/wifi_setup_model/owner_record.cpp', '-o', str(OUT / 'runtime-tests')]
    records = []
    inputs = ['src/wifi_setup_runtime.cpp', 'src/wifi_setup_nvs.cpp',
              'include/ble_feature.h', 'include/backup_worker.h', 'include/backup_memory.h',
              'include/config_loader.h', 'include/pro_ble_lifecycle.h',
              'include/pro_device.h', 'include/wifi_setup_runtime.h',
              'include/wifi_setup_radio.h',
              'include/wifi_setup_nvs_release.h', 'src/wifi_setup_nvs_release.cpp',
              'include/wifi_setup_cache_lab.h', 'src/wifi_setup_cache_lab.cpp',
              'test/nvs_cache_release/stubs/esp_partition.h',
              'test/nvs_cache_release/stubs/nvs_flash.h',
              'test/nvs_cache_release/stubs/esp_err.h',
              'include/pro_wifi_reconnect.h',
              'test/backup_integration/runtime_handoff.test.cpp',
              'test/backup_integration/runtime_stubs.h',
              'test/backup_integration/runtime_wire.test.cpp',
              'test/wifi_integration/stubs/common.h',
              'lib/wifi_setup_model/wifi_setup_model.cpp',
              'lib/wifi_setup_model/wifi_profile_store.cpp',
              'lib/wifi_setup_model/wifi_wire.cpp',
              'lib/wifi_setup_model/wifi_wire.h',
              'lib/wifi_setup_model/owner_record.cpp']
    source_hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                     for name in inputs}

    def run(name, args):
        result = subprocess.run(args, cwd=ROOT, env=ENV, capture_output=True,
                                text=True, timeout=180)
        output = result.stdout + result.stderr
        (OUT / (name + '.log')).write_text(output)
        diagnostic = bool(re.search(r'runtime error:|AddressSanitizer:|LeakSanitizer:|UndefinedBehaviorSanitizer:', output))
        passed = result.returncode == 0 and not result.stderr and not diagnostic
        records.append({'name': name, 'command': args, 'exit': result.returncode,
                        'stderr_empty': not result.stderr, 'sanitizer_diagnostic': diagnostic,
                        'passed': passed})
        print(('PASS ' if passed else 'FAIL ') + name, flush=True)
        (OUT / 'results.json').write_text(json.dumps({
            'desktop_only': True, 'physical_device_access': False,
            'actual_source': 'src/wifi_setup_runtime.cpp',
            'simulated': ['worker control', 'USB', 'BLE', 'radio', 'NVS', 'clock', 'heap'],
            'sanitizers': ['ASan', 'UBSan', 'LeakSanitizer'],
            'sanitizer_environment': {key: ENV[key] for key in ['ASAN_OPTIONS', 'UBSAN_OPTIONS']},
            'source_sha256': source_hashes,
            'records': records, 'passed': all(r['passed'] for r in records)}, indent=2) + '\n')
        if not passed:
            print(output[-6000:], file=sys.stderr)
            raise SystemExit(1)

    late_command = [*command[:-2], '-DCSP_ENABLE_BLE_RELEASE_SETTLEMENT=1',
                    '-DCSP_ENABLE_BLE_EXTENDED_SETTLEMENT=1',
                    '-o', str(OUT / 'runtime-late-settlement-tests')]
    run('late-settlement-build', late_command)
    for scenario in ['release-slow', 'release-slow-rollover', 'release-slow-deadline',
                     'release-late-network', 'release-late-network-deadline',
                     'normal', 'repeated', 'ble-stop-fail', 'release-fail',
                     'reinit-fail', 'heap-return-low', 'same-profile-save']:
        run('late-' + scenario, [str(OUT / 'runtime-late-settlement-tests'), str(fixture), scenario])
    run('build', command)
    assert all(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == value
               for name, value in source_hashes.items()), 'Source changed during compilation; rerun the test build'
    for scenario in ['ntp-short-lived', 'ntp-long-lived', 'ntp-max-bound', 'ntp-empty', 'ntp-too-long',
                     'normal', 'repeated', 'wifi-reconnect', 'wifi-failover', 'owner-missing', 'owner-corrupt',
                     'config-invalid', 'init-fail', 'start-failure',
                     'quiesce-timeout', 'cancel-retiring', 'stale-retirement', 'reap-timeout',
                     'heap-start-low', 'ble-stop-fail', 'release-fail',
                     'reinit-fail', 'heap-return-low', 'unauthenticated-api',
                     'unauthenticated-backup-status', 'backup-status', 'serial-backup-status', 'serial-memory-status',
                     'fixture-config', 'same-profile-save']:
        run(scenario, [str(OUT / 'runtime-tests'), str(fixture), scenario])
    fair_command = [*command[:-2], '-DCSP_ENABLE_WIFI_FAIR_RETRY=1',
                    '-o', str(OUT / 'runtime-fair-tests')]
    run('fair-retry-build', fair_command)
    for scenario in ['wifi-eight-saved', 'wifi-reconnect', 'wifi-failover', 'normal',
                     'quiesce-timeout', 'ble-stop-fail', 'same-profile-save']:
        run('fair-' + scenario, [str(OUT / 'runtime-fair-tests'), str(fixture), scenario])
    settlement_command = [*command[:-2], '-DCSP_ENABLE_WIFI_FAIR_RETRY=1',
                          '-DCSP_ENABLE_BLE_RELEASE_SETTLEMENT=1',
                          '-o', str(OUT / 'runtime-settlement-tests')]
    run('settlement-build', settlement_command)
    for scenario in ['release-delayed', 'release-deadline', 'release-rollover',
                     'normal', 'repeated', 'ble-stop-fail', 'release-fail',
                     'reinit-fail', 'heap-return-low', 'same-profile-save']:
        run('settlement-' + scenario, [str(OUT / 'runtime-settlement-tests'), str(fixture), scenario])
    cache_command = [*command[:-2], '-DCSP_ENABLE_WIFI_FAIR_RETRY=1',
                     '-DCSP_ENABLE_BLE_RELEASE_SETTLEMENT=1',
                     '-DCSP_ENABLE_BLE_NVS_CACHE_RELEASE=1',
                     '-Itest/nvs_cache_release/stubs', 'src/wifi_setup_nvs_release.cpp',
                     '-o', str(OUT / 'runtime-cache-tests')]
    run('cache-build', cache_command)
    for scenario in ['nvs-release-normal', 'nvs-deinit-fail', 'nvs-init-fail',
                     'nvs-flash-changed', 'nvs-heap-low', 'normal', 'repeated',
                     'ble-stop-fail', 'release-fail', 'reinit-fail', 'heap-return-low',
                     'same-profile-save', 'wifi-eight-saved']:
        run('cache-' + scenario, [str(OUT / 'runtime-cache-tests'), str(fixture), scenario])
    late_cache_command = [*cache_command[:-2], '-DCSP_ENABLE_BLE_EXTENDED_SETTLEMENT=1',
                          '-o', str(OUT / 'runtime-late-cache-tests')]
    run('late-cache-build', late_cache_command)
    for scenario in ['release-slow', 'release-slow-rollover', 'release-slow-deadline',
                     'release-late-network', 'release-late-network-deadline',
                     'nvs-release-normal', 'nvs-deinit-fail', 'nvs-init-fail',
                     'nvs-flash-changed', 'ble-stop-fail', 'release-fail', 'reinit-fail',
                     'normal', 'repeated', 'same-profile-save']:
        run('late-cache-' + scenario, [str(OUT / 'runtime-late-cache-tests'), str(fixture), scenario])
    fixture_command = [*command[:-2], '-DCSP_BACKUP_FIXTURE_ONLY=1',
                       '-o', str(OUT / 'runtime-fixture-tests')]
    run('fixture-build', fixture_command)
    for scenario in ['ntp-short-lived', 'ntp-max-bound', 'fixture-config', 'normal', 'repeated', 'serial-backup-status', 'same-profile-save']:
        run('fixture-' + scenario, [str(OUT / 'runtime-fixture-tests'), str(fixture), scenario])
    for mode in ['default', 'disabled', 'enabled']:
        feature = [] if mode == 'default' else ['-DCSP_ENABLE_BACKUP_SETUP=' + ('1' if mode == 'enabled' else '0')]
        binary = str(OUT / ('wire-' + mode))
        run('wire-' + mode + '-build', [str(CLANG), *flags, *feature,
            'test/backup_integration/runtime_wire.test.cpp',
            'lib/wifi_setup_model/wifi_setup_model.cpp',
            'lib/wifi_setup_model/wifi_wire.cpp', '-o', binary])
        run('wire-' + mode, [binary])


if __name__ == '__main__':
    main()
