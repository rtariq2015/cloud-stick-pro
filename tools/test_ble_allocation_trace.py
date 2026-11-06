"""Desktop-only allocation diagnostic gates. Never opens or uploads to hardware."""
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/allocation-diagnostics';OUT.mkdir(exist_ok=True)
CORE=Path(os.environ.get('PLATFORMIO_CORE_DIR','/config/.unmanic/research/cloud-stick-platformio'))
deps=CORE.parent/'cloud-stick-auth-native-deps/root/usr'
env=dict(os.environ)
if deps.exists():
    env['PATH']=str(deps/'lib/llvm-18/bin')+':'+env.get('PATH','')
    env['C_INCLUDE_PATH']=str(deps/'include')
    env['LIBRARY_PATH']=str(deps/'lib/x86_64-linux-gnu')
    env['LD_LIBRARY_PATH']=str(deps/'lib/x86_64-linux-gnu')
env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
records=[]


def run(name,args,settings=None):
    r=subprocess.run(args,cwd=ROOT,env=settings or env,capture_output=True,text=True,timeout=180)
    (OUT/(name+'.log')).write_text(r.stdout+r.stderr)
    records.append({'name':name,'command':list(map(str,args)),'exit':r.returncode,'stderr_empty':not r.stderr})
    if r.returncode or r.stderr:raise RuntimeError(name+' failed or wrote stderr; see its retained log')
    print('PASS',name,flush=True)
    return r


flags=['-std=c++17','-pthread','-Wall','-Wextra','-Werror','-g',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
       '-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_BLE_ISOLATED_TEST=1',
       '-DCSP_BLE_SYNTHETIC_ONLY=1','-DCSP_BLE_ALLOC_TRACE=1']
run('core-build',['g++',*flags,'-O1','-Itest/alloc_trace_stubs','-Iinclude','test/alloc_trace.test.cpp','-o',str(OUT/'alloc-trace-tests')])
r=run('core-tests',[str(OUT/'alloc-trace-tests')]);assert not r.stderr
benchflags=[f for f in flags if not f.startswith(('-DCSP_BLE_ISOLATED_TEST=','-DCSP_BLE_SYNTHETIC_ONLY='))]
run('network-wrapper-build',['g++',*benchflags,'-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_USB_BENCH_AUTOMATION=1','-O1','-Itest/alloc_trace_stubs','-Iinclude','test/alloc_trace.test.cpp','-o',str(OUT/'network-wrapper-tests')])
run('network-wrapper-tests',[str(OUT/'network-wrapper-tests')])
jsonflags=['-Itest/milestone1_corrections/isolation_stubs','-Iinclude','-isystem',
           '.pio/libdeps/tdongle_s3_ble_m1_isolated/ArduinoJson/src']
# Match the existing isolated-runtime GCC O0 gate. Retain and disposition the
# independently observed GCC O1 ArduinoJson iterator warning; suppress nothing.
for compiler,opt,label in [('g++','-O0','gcc-o0'),('clang++','-O1','clang-o1')]:
    binary=OUT/('json-tests-'+label)
    run('json-build-'+label,[compiler,*flags,opt,*jsonflags,'test/alloc_trace_json.test.cpp','-o',str(binary)])
    r=run('json-tests-'+label,[str(binary)]);assert not r.stderr
run('offline-page-validation',['python3','test/test_ble_trace_pages.py'])

run('adapter-baseline-build',['python3','test/milestone1_corrections/build_adapter_tests.py'])
build=json.loads((ROOT/'artifacts/milestone1-corrections/adapter/build.json').read_text())
commands=build['commands']
base=next(cmd for cmd in commands if any(str(x).endswith('/actual_adapter_sdk.cpp') for x in cmd))
src=next(x for x in base if str(x).endswith('/actual_adapter_sdk.cpp'))
run('sdk-main-object',base[:base.index(src)]+['-DCSP_BLE_ALLOC_TRACE=1','-Dmain=csp_test_sdk_main','-c',src,'-o',str(OUT/'sdk-main.o')])
for name,src in [('trace-object','src/ble_alloc_trace.cpp'),('control-object','test/alloc_trace_sdk_main.cpp')]:
    run(name,['g++',*flags,'-O1','-Itest/alloc_trace_stubs','-Iinclude','-c',src,'-o',str(OUT/(name+'.o'))])
objects=[cmd[cmd.index('-o')+1] for cmd in commands if cmd[0]=='gcc']
wrap=['malloc','calloc','realloc','free','heap_caps_malloc','heap_caps_calloc',
      'heap_caps_realloc','heap_caps_free','heap_caps_aligned_alloc','heap_caps_aligned_calloc']
binary=OUT/'sdk-trace-asan'
run('sdk-trace-link',['g++',*flags,'-O1',str(OUT/'sdk-main.o'),str(OUT/'trace-object.o'),
                     str(OUT/'control-object.o'),*objects,
                     *['-Wl,--wrap='+name for name in wrap],'-lmbedtls','-lmbedcrypto',
                     '-lmbedx509','-lprotobuf-c','-o',str(binary)])
settings=dict(env,CSP_ADAPTER_HOST=str(binary))
r=run('actual-sdk-with-allocation-wrappers',['node','--test','test/milestone1_corrections/reject_latch.test.mjs'],settings)
assert not r.stderr
result={'passed':True,'physical_device_access':False,'commands':records,
        'sanitizers':'Nonrecovering ASan/UBSan, leak checks, empty runtime stderr required',
        'scope':'Actual observer/core/exporter and wrapped real SDK crypto on desktop; RTOS/radio/heap_caps primitives are fake; no target latency/cache-off claim',
        'gcc_o1_warning':'Initial diagnostic retained in json-build-gcc-o1-warning.log; pinned ArduinoJson end-iterator nextId_ warning at GCC O1. Existing GCC O0 plus independent Clang O1 run without suppression. No vendor change.'}
(OUT/'desktop-tests.json').write_text(json.dumps(result,indent=2)+'\n')
