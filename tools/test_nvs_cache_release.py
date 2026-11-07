"""Strict actual cache-release source tests; synthetic SDK/flash, no device."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/nvs-cache-release/native'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    names = ['include/wifi_setup_nvs_release.h', 'src/wifi_setup_nvs_release.cpp',
             'include/wifi_setup_cache_lab.h', 'src/wifi_setup_cache_lab.cpp',
             'test/nvs_cache_release/lab.test.cpp',
             'test/nvs_cache_release/release.test.cpp', 'tools/test_nvs_cache_release.py',
             'lib/wifi_setup_model/wifi_setup_model.cpp', 'include/ble_feature.h']
    names += sorted(str(p.relative_to(ROOT)) for p in (ROOT / 'test/nvs_cache_release/stubs').glob('*.h'))
    sources = {n: hashlib.sha256((ROOT / n).read_bytes()).hexdigest() for n in names}
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    binary = str(OUT / 'cache-tests')
    command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-g', '-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined',
               '-fno-sanitize-recover=all', '-Itest/nvs_cache_release/stubs',
               '-Iinclude', '-Ilib/wifi_setup_model', '-DCSP_ENABLE_WIFI_SETUP=1',
               '-DCSP_ENABLE_BLE_MANAGEMENT=1', '-DCSP_ENABLE_BACKUP_SETUP=1',
               '-DCSP_ENABLE_BLE_NVS_CACHE_RELEASE=1', '-Wl,--wrap=malloc,--wrap=free',
               'test/nvs_cache_release/release.test.cpp', 'src/wifi_setup_nvs_release.cpp',
               'lib/wifi_setup_model/wifi_setup_model.cpp', '-o', binary]
    records = []
    runs = [('build', command)] + [(name, [binary, name]) for name in [
        'normal', 'missing', 'wrong-address', 'wrong-size', 'allocation-fail',
        'read-before-fail', 'read-after-fail', 'read-tail-fail', 'deinit-fail',
        'init-fail', 'flash-changed', 'heap-still-low']]
    lab_binary = str(OUT / 'lab-tests')
    lab_command = [*command[:command.index('-Wl,--wrap=malloc,--wrap=free')],
                   '-DCSP_USB_BENCH_AUTOMATION=1', '-DCSP_NVS_CACHE_LAB=1',
                   'test/nvs_cache_release/lab.test.cpp', 'src/wifi_setup_cache_lab.cpp',
                   '-o', lab_binary]
    runs += [('lab-build', lab_command)] + [('lab-' + name, [lab_binary, name]) for name in [
        'normal', 'low-space', 'reserved-space', 'stats-fail', 'open-fail', 'write-first-fail',
        'write-later-fail', 'commit-fail', 'read-fail', 'read-mismatch', 'clear-erase-fail']]
    for name, args in runs:
        result = subprocess.run(args, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        (OUT / (name + '.log')).write_text(output)
        diagnostic = bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:', output))
        passed = result.returncode == 0 and not result.stderr and not diagnostic
        stable = all(hashlib.sha256((ROOT / n).read_bytes()).hexdigest() == h for n, h in sources.items())
        records.append({'name': name, 'command': args, 'exit': result.returncode, 'passed': passed})
        (OUT / 'results.json').write_text(json.dumps({'physical_device_access': False,
            'simulated': ['NVS SDK', 'partition reads', 'allocation failure', 'heap samples'],
            'source_sha256': sources, 'source_stable': stable, 'records': records,
            'passed': stable and all(r['passed'] for r in records)}, indent=2) + '\n')
        print(('PASS ' if passed and stable else 'FAIL ') + name, flush=True)
        if not passed or not stable:
            print(output[-4000:]);return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
