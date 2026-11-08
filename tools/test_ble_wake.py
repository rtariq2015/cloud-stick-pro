#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Native request-policy and diagnostic-observer tests; never opens hardware."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/button-free-work/native'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    names=['include/ble_wake.h','src/ble_wake_model.cpp','include/ble_release_trace.h',
           'src/ble_release_trace.cpp','test/ble_wake/policy.test.cpp',
           'test/ble_wake/release_trace.test.cpp','tools/test_ble_wake.py',
           'include/pro_alloc_trace.h','include/pro_heap_span.h','include/ble_feature.h']
    names+=sorted(str(p.relative_to(ROOT)) for p in (ROOT/'test/alloc_trace_stubs').rglob('*.h'))
    sources={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names}
    flags=['-std=c++17','-Wall','-Wextra','-Werror','-O1','-g','-fno-omit-frame-pointer',
           '-fsanitize=address,undefined','-fno-sanitize-recover=all','-Iinclude']
    compiler=os.environ.get('CXX','g++');policy=str(OUT/'policy');observer=str(OUT/'observer')
    selected=['-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1',
              '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_USB_BENCH_AUTOMATION=1',
              '-DCSP_PRODUCT_RELEASE_LAB_PROFILE=1','-DCSP_BLE_RELEASE_DIAGNOSTICS=1']
    commands=[('policy-build',[compiler,*flags,'test/ble_wake/policy.test.cpp','src/ble_wake_model.cpp','-o',policy]),('policy',[policy]),
              ('observer-build',[compiler,*flags,*selected,'-Itest/alloc_trace_stubs',
               'test/ble_wake/release_trace.test.cpp','src/ble_release_trace.cpp',
               *['-Wl,--wrap='+name for name in ['malloc','calloc','free','heap_caps_malloc','heap_caps_calloc','heap_caps_free']],'-o',observer]),('observer',[observer])]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    records=[]
    for name,args in commands:
        result=subprocess.run(args,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
        (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
        passed=result.returncode==0 and not result.stderr
        records.append({'name':name,'passed':passed,'exit':result.returncode})
        stable=all(hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h for n,h in sources.items())
        (OUT/'results.json').write_text(json.dumps({'passed':stable and all(r['passed'] for r in records),
            'physical_device_access':False,'simulated':['proof verifier','RNG','heap walk','RTOS synchronization'],
            'source_sha256':sources,'source_stable':stable,'records':records},indent=2)+'\n')
        print(('PASS ' if passed and stable else 'FAIL ')+name,flush=True)
        if not passed or not stable:print((result.stdout+result.stderr)[-4000:]);return 1
    return 0

if __name__=='__main__':raise SystemExit(main())
