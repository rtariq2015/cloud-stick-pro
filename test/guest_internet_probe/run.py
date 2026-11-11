#!/usr/bin/env python3
"""Run the actual guest internet probe with synthetic SDK/RTOS, no network/device."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
compiler=shutil.which('clang++') or shutil.which('g++')
if not compiler:raise SystemExit('C++ compiler required')
with tempfile.TemporaryDirectory(prefix='csp-guest-probe-') as tmp:
    binary=Path(tmp)/'probe-test'
    subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DCSP_ENABLE_GUEST_DIAGNOSTIC=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_USB_BENCH_AUTOMATION=1','-DCSP_ENABLE_LOCAL_DISCOVERY=1','-I'+str(root/'test/guest_internet_probe/stubs'),'-I'+str(root/'include'),str(root/'test/guest_internet_probe/probe.test.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
