#!/usr/bin/env python3
"""Strict saved-network policy tests; no device, credentials or network access."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/wifi-fallback/policy'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    names = ['include/pro_wifi_reconnect.h', 'test/backup_integration/reconnect.test.cpp',
             'lib/wifi_setup_model/wifi_setup_model.h',
             'lib/wifi_setup_model/wifi_profile_store.h', 'include/pro_device.h',
             'include/ble_feature.h', 'tools/test_wifi_reconnect.py']
    hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in names}
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    records = []
    for mode, defines in [('historical', []), ('disabled', ['-DCSP_ENABLE_WIFI_FAIR_RETRY=0']),
                          ('fair', ['-DCSP_ENABLE_WIFI_FAIR_RETRY=1'])]:
        binary = str(OUT / mode)
        command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-g', '-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
                   '-fno-sanitize-recover=all', '-Iinclude', '-Ilib/wifi_setup_model',
                   *defines, 'test/backup_integration/reconnect.test.cpp', '-o', binary]
        for label, args in [(mode + '-build', command), (mode, [binary])]:
            result = subprocess.run(args, cwd=ROOT, env=env, capture_output=True,
                                    text=True, timeout=120)
            output = result.stdout + result.stderr
            (OUT / (label + '.log')).write_text(output)
            diagnostic = bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:', output))
            passed = result.returncode == 0 and not result.stderr and not diagnostic
            records.append({'name': label, 'command': args, 'exit': result.returncode,
                            'stderr_empty': not result.stderr, 'passed': passed})
            stable = all(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest
                         for name, digest in hashes.items())
            (OUT / 'results.json').write_text(json.dumps({
                'scope': 'Native policy; simulated connection timings only',
                'physical_device_access': False, 'source_sha256': hashes,
                'source_stable': stable, 'records': records,
                'passed': stable and all(record['passed'] for record in records)}, indent=2) + '\n')
            print(('PASS ' if passed and stable else 'FAIL ') + label, flush=True)
            if not passed or not stable:
                print(output[-4000:])
                return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
