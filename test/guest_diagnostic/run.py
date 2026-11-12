#!/usr/bin/env python3
"""Run actual diagnostic orchestration with fake radio/probe/USB/lease APIs.

No dongle, network, credentials, storage or cloud account is used. The internet
transport itself and integration with the owner runtime are separate tests.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SCENARIOS = [
    'denied', 'cancel_idle', 'busy_reject', 'disconnect_failure', 'scan_start_failure',
    'scan_timeout', 'scan_fault', 'absent',
    *['auth_' + str(auth) for auth in range(1, 17)],
    'conflicting', 'truncated', 'connect_start_failure', 'two_attempt_limit',
    'connect_fault', 'missing_netif', 'lease_read_failure',
    'probe_allocation_failure', 'cancel_scan', 'cancel_probe',
    'identity_ssid', 'identity_ip', 'identity_state', 'success',
    'restart_clears_result', 'cancel_complete', 'later_identity_ssid',
    'later_identity_ip', 'later_identity_state', 'dns_fallback', 'dns_failure', 'clock_failure',
    'http_redirect', 'http_intercept', 'https_redirect', 'tls_failure',
]


def main():
    compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise SystemExit('A C++17 compiler is required')
    with tempfile.TemporaryDirectory(prefix='csp-guest-diagnostic-') as temporary:
        executable = str(Path(temporary) / 'orchestration')
        subprocess.run([
            compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g', '-O1',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
            '-DCSP_ENABLE_GUEST_DIAGNOSTIC=1', '-DCSP_ENABLE_WIFI_SETUP=1',
            '-DCSP_ENABLE_BLE_MANAGEMENT=1', '-DCSP_ENABLE_BACKUP_SETUP=1',
            '-DCSP_USB_BENCH_AUTOMATION=1', '-DCSP_ENABLE_LOCAL_DISCOVERY=1',
            '-I' + str(HERE / 'stubs'), '-I' + str(ROOT / 'include'),
            '-I' + str(ROOT / 'lib/wifi_setup_model'),
            str(HERE / 'orchestration.test.cpp'), str(ROOT / 'src/guest_diagnostic.cpp'),
            '-o', executable,
        ], check=True)
        failures = []
        for scenario in SCENARIOS:
            result = subprocess.run([executable, scenario], text=True, capture_output=True,
                                    timeout=15)
            if result.returncode:
                failures.append(scenario)
                print(result.stderr, end='')
            else:
                print(result.stdout, end='')
        print(f'{len(SCENARIOS)-len(failures)}/{len(SCENARIOS)} native orchestration scenarios passed')
        if failures:
            raise SystemExit('Failed: ' + ', '.join(failures))


if __name__ == '__main__':
    main()
