"""Run actual adapter regressions with/without advisory collection reuse.

HTTP/TLS/SD/clock are simulated. Counts measure requests, not real upload speed.
The test never accesses a serial device, owner file or cloud account.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/dav-collection-cache'
OUT.mkdir(parents=True,exist_ok=True)
records=[]
sources=['src/webdav_client.cpp','include/webdav_client.h','src/pro_core.cpp',
         'src/cloud_request_trace.cpp','include/cloud_request_trace.h',
         'test/dav_collection_cache/trace.test.cpp','test/dav_collection_cache/trace_stubs/Arduino.h',
         'test/dav_collection_cache/adapter.test.cpp',
         'test/dav_collection_cache/stubs/HTTPClient.h','test/dav_collection_cache/run.py']
hashes={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
compiler=os.environ.get('CXX',shutil.which('clang++') or 'g++')
for name,flags,allowed,reason in [
    ('default',[],True,'DAV collection cache'),
    ('invalid-value',['-DCSP_ENABLE_DAV_COLLECTION_CACHE=2'],False,'DAV collection cache'),
    ('missing-identity',['-DCSP_ENABLE_DAV_COLLECTION_CACHE=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1'],False,'DAV collection cache'),
    ('missing-worker',['-DCSP_ENABLE_DAV_COLLECTION_CACHE=1','-DCSP_PRODUCT_UPLOAD_DEV_PROFILE=1'],False,'DAV collection cache'),
    ('trace-invalid-value',['-DCSP_ENABLE_CLOUD_REQUEST_TRACE=2'],False,'Cloud request trace'),
    ('trace-missing-identity',['-DCSP_ENABLE_CLOUD_REQUEST_TRACE=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1'],False,'Cloud request trace'),
    ('trace-missing-worker',['-DCSP_ENABLE_CLOUD_REQUEST_TRACE=1','-DCSP_PRODUCT_UPLOAD_DEV_PROFILE=1'],False,'Cloud request trace'),
]:
    result=subprocess.run([compiler,'-std=c++17','-fsyntax-only','-x','c++',
        '-Itest/backup_integration/dav_stubs','-Iinclude',*flags,'-'],
        input='#include "webdav_client.h"\n',cwd=ROOT,env=env,capture_output=True,text=True,timeout=60)
    passed=(result.returncode==0)==allowed
    if not allowed:passed=passed and reason in result.stderr
    records.append({'name':'admission-'+name,'passed':passed,'expected_compile_success':allowed})
    print('admission-'+name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:raise SystemExit('cache feature admission failed')
for cache in (0,1):
    binary=OUT/f'adapter-{cache}'
    command=[compiler,'-std=c++17','-g','-O1',
      '-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
      '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
      '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1',
      '-DCSP_PRODUCT_UPLOAD_DEV_PROFILE=1',
      f'-DCSP_ENABLE_DAV_COLLECTION_CACHE={cache}',
      f'-DCSP_ENABLE_CLOUD_REQUEST_TRACE={cache}','-pthread',
      '-Itest/dav_collection_cache/stubs','-Itest/backup_integration/dav_stubs',
      '-Iinclude','-Ilib/tinyxml2/src','src/webdav_client.cpp','src/pro_core.cpp',
      'lib/tinyxml2/src/tinyxml2.cpp','src/cloud_request_trace.cpp','test/dav_collection_cache/adapter.test.cpp',
      '-lcrypto','-Wl,--wrap=fopen','-o',str(binary)]
    for name,args in [(f'build-{cache}',command),(f'run-{cache}',[str(binary)])]:
        result=subprocess.run(args,cwd=ROOT,env=env,capture_output=True,text=True,timeout=180)
        output=result.stdout+result.stderr;(OUT/(name+'.log')).write_text(output)
        passed=result.returncode==0 and not result.stderr and all(secret not in output for secret in ('synthetic-user','synthetic-secret','synthetic-ca'))
        records.append({'name':name,'passed':passed,'exit':result.returncode})
        print(name,'PASS' if passed else 'FAIL',flush=True)
        if not passed:print(output[-6000:]);raise SystemExit(1)
        if name.startswith('run-'):
            print(next(line for line in output.splitlines() if line.startswith('BATCH ')))
trace=OUT/'trace-tests'
command=[compiler,'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-pthread',
    '-fsanitize=address,undefined','-fno-sanitize-recover=all',
    '-DCSP_ENABLE_CLOUD_REQUEST_TRACE=1','-DCSP_PRODUCT_UPLOAD_DEV_PROFILE=1',
    '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1',
    '-Itest/dav_collection_cache/trace_stubs','-Iinclude',
    'src/cloud_request_trace.cpp','test/dav_collection_cache/trace.test.cpp','-o',str(trace)]
for name,args in [('trace-build',command),('trace-cases',[str(trace)])]:
    result=subprocess.run(args,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
    (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
    passed=result.returncode==0 and not result.stderr;records.append({'name':name,'passed':passed,'exit':result.returncode})
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:print(result.stdout+result.stderr);raise SystemExit(1)
assert all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==value for name,value in hashes.items())
(OUT/'results.json').write_text(json.dumps({'passed':True,'physical_device':False,
    'real_cloud':False,'actual_adapter':'src/webdav_client.cpp',
    'simulated':['HTTP','TLS','SD','clock'],
    'source_sha256':hashes,'records':records},indent=2)+'\n')
