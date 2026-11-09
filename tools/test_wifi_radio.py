"""Actual radio source under sanitizers; SDK, events, DHCP and RF are simulated."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ['-std=c++17', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
         '-fno-omit-frame-pointer', '-no-pie', '-Iinclude',
         '-Ilib/wifi_setup_model', '-Itest/wifi_integration/radio_stubs',
         '-DCSP_ENABLE_BLE_MANAGEMENT=1', '-DCSP_ENABLE_WIFI_SETUP=1']
MODES = {
    'historical': [],
    'release': ['-DCSP_USB_BENCH_AUTOMATION=1', '-DCSP_BENCH_RADIO_RELEASE=1'],
    'diagnostic': ['-DCSP_USB_BENCH_AUTOMATION=1', '-DCSP_BENCH_RADIO_RELEASE=1',
                   '-DCSP_ENABLE_WIFI_ATTEMPT_TRACE=1', '-DCSP_ENABLE_WIFI_STANDARD_SCAN=1'],
}
with tempfile.TemporaryDirectory(prefix='csp-radio-test-') as directory:
    for name, defines in MODES.items():
        binary = Path(directory) / name
        subprocess.run([os.environ.get('CXX', 'clang++'), *FLAGS, *defines,
                        'test/wifi_integration/radio.test.cpp',
                        'lib/wifi_setup_model/wifi_setup_model.cpp', '-o', str(binary)],
                       cwd=ROOT, check=True, timeout=90)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=30)
subprocess.run(['python3', 'test/wifi_integration/bench_feature_flags.test.py'],
               cwd=ROOT, check=True, timeout=60)
