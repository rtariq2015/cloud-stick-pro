#!/usr/bin/env python3
"""Actual owner-loop integration with synthetic I/O; no hardware/network."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
from create_wifi_owner_card import create_record, SYNTHETIC_PASSWORD
from pinned_test_dependencies import arduino_json_include
SCENARIOS = ['normal', 'invalid-config', 'owner-missing', 'dashboard-stop-failure',
             'ble-busy', 'bench-busy', 'cancel-and-resume', 'provisioning-after-cancel']

def main():
    deps = Path.home() / 'tool.plugin-builder/build/research/cloud-stick-auth-native-deps/root/usr'
    pinned = deps / 'lib/llvm-18/bin/clang++'
    compiler = os.environ.get('CSP_TEST_CLANGXX') or os.environ.get('CXX') or shutil.which('clang++') or (str(pinned) if pinned.is_file() else None)
    if not compiler:
        raise SystemExit('Clang with C++17 and sanitizers is required for the retained runtime fixtures')
    env = dict(os.environ)
    if str(compiler) == str(pinned):
        env['LD_LIBRARY_PATH'] = str(deps / 'lib/x86_64-linux-gnu')
    with tempfile.TemporaryDirectory(prefix='csp-guest-runtime-') as temp:
        fixture = Path(temp) / 'synthetic-owner.bin'
        fixture.write_bytes(create_record('001122334455', SYNTHETIC_PASSWORD))
        binary = Path(temp) / 'runtime'
        command = [compiler, '-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-pthread',
                   '-Iinclude', '-Ilib/wifi_setup_model', '-Itest/wifi_integration/stubs',
                   '-isystem', str(arduino_json_include(ROOT)), '-include', str(HERE / 'stubs.h')]
        defines = ['CSP_ENABLE_BLE_MANAGEMENT=1', 'CSP_ENABLE_WIFI_SETUP=1',
                   'CSP_ENABLE_BACKUP_SETUP=1', 'CSP_USB_BENCH_AUTOMATION=1',
                   'CSP_ENABLE_GUEST_DIAGNOSTIC=1', 'CSP_ENABLE_LOCAL_DISCOVERY=1',
                   'CSP_ENABLE_LOCAL_DASHBOARD=1', 'CSP_PRODUCT_DASHBOARD_TRIAL=1',
                   'CONFIG_BT_NIMBLE_ENABLED=1', 'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1',
                   'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
        command += ['-D' + d for d in defines]
        command += [str(HERE / 'runtime.test.cpp'), 'src/wifi_setup_runtime.cpp', 'src/wifi_setup_nvs.cpp',
                    'lib/wifi_setup_model/wifi_setup_model.cpp', 'lib/wifi_setup_model/wifi_profile_store.cpp',
                    'lib/wifi_setup_model/wifi_wire.cpp', 'lib/wifi_setup_model/owner_record.cpp', '-o', str(binary)]
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        failed = []
        for scenario in SCENARIOS:
            result = subprocess.run([str(binary), str(fixture), scenario], cwd=ROOT,
                                    text=True, capture_output=True, timeout=20, env=env)
            if result.returncode:
                failed.append(scenario)
                print(result.stderr, end='')
            else:
                print(result.stdout, end='')
        print(f'{len(SCENARIOS)-len(failed)}/{len(SCENARIOS)} actual owner-loop scenarios passed')
        if failed:
            raise SystemExit('Failed: ' + ', '.join(failed))
if __name__ == '__main__':
    main()
