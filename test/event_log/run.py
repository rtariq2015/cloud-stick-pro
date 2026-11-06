#!/usr/bin/env python3
"""Strict native event-log/fault/actual-worker tests. All files synthetic."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/event-log'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
         LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
FLAGS=['-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-fno-omit-frame-pointer',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread']
RECORD=[]
def run(name,command,expect_failure=False):
 result=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=60)
 log=OUT/(name+'.log');log.write_text(result.stdout+result.stderr)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',result.stdout+result.stderr))
 passed=(result.returncode!=0 and 'CSP_ENABLE_EVENT_LOG must be zero or one' in result.stderr) if expect_failure else result.returncode==0 and not result.stderr and not diagnostic
 RECORD.append({'name':name,'command':list(map(str,command)),'exit':result.returncode,'stderr_empty':not result.stderr,
                'expected_compile_rejection':expect_failure,'sanitizer_diagnostic':diagnostic,'pass':passed,
                'log':str(log.relative_to(ROOT)),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
 if not passed:raise RuntimeError(name+' failed: '+str(log))
 print('PASS',name,flush=True)
 return result.stdout
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 tracked=['include/pro_event_log.h','src/pro_event_log.cpp','src/sync_engine.cpp',
          'test/event_log/event_log.test.cpp','test/event_log/worker.test.cpp','test/event_log/io.h']
 before={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked}
 try:
  with tempfile.TemporaryDirectory(prefix='csp-event-log-native-') as temporary:
   base=Path(temporary)
   common=['-include','test/event_log/io.h','-Iinclude']
   component=[CLANG,*FLAGS,'-DCSP_ENABLE_EVENT_LOG=1','-DCSP_EVENT_LOG_TESTING=1',
              '-DCSP_EVENT_LOG_ROOT="'+str(base/'component')+'"',*common,
              'src/pro_event_log.cpp','test/event_log/event_log.test.cpp','-o',OUT/'event-tests']
   run('component-build',component);output=run('component-tests',[OUT/'event-tests'])
   dimensions=re.search(r'static_bytes=(\d+) snapshot_max_bytes=(\d+)',output)
   assert dimensions and int(dimensions[1])<=512 and int(dimensions[2])<=4096
   cases=int(re.search(r'(\d+) sanitized event-log cases passed',output)[1])
   for enabled in [0,1]:
    binary=OUT/('worker-'+str(enabled))
    flags=['-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1',
           '-DCSP_ENABLE_EVENT_LOG='+str(enabled),'-DCSP_EVENT_LOG_ROOT="'+str(base/('worker'+str(enabled)))+'"']
    run('worker-'+str(enabled)+'-build',[CLANG,*FLAGS,*flags,'-Itest/backup_integration/stubs',*common,
        'src/sync_engine.cpp','src/pro_event_log.cpp','test/event_log/worker.test.cpp','-o',binary])
    for mode in ['tls','snapshot','io-failure','coalesce']:run('worker-'+str(enabled)+'-'+mode,[binary,mode])
   run('disabled-object-build',[CLANG,'-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','-c','src/pro_event_log.cpp','-o',OUT/'disabled.o'])
   symbols=run('disabled-object-symbols',['nm','--defined-only',OUT/'disabled.o'])
   assert not symbols.strip(),'Default-off logger must contain no storage/ring symbols'
   run('invalid-flag-build',[CLANG,'-std=c++17','-DCSP_ENABLE_EVENT_LOG=2','-Iinclude','-c','src/pro_event_log.cpp','-o',OUT/'invalid.o'],expect_failure=True)
   # Existing actual-worker regression unchanged, with default event flag off.
   run('existing-worker-build',[CLANG,*FLAGS,'-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1',
       '-Itest/backup_integration/stubs','-Iinclude','src/sync_engine.cpp','test/backup_integration/worker.test.cpp','-o',OUT/'existing-worker'])
   existing=run('existing-worker-tests',[OUT/'existing-worker'])
   for mode in ['fs-preexisting','fs-partial','fs-end-fail','fs-begin-fail']:run('existing-worker-'+mode,[OUT/'existing-worker',mode])
   existing_cases=int(re.search(r'(\d+) actual adapter cases passed',existing)[1])+4
  after={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked}
  assert before==after,'Source changed during test run'
  result={'pass':True,'scope':'Native actual event codec/POSIX fault adapter and actual worker with synthetic RTOS/USB/TLS/LittleFS',
          'component_cases':cases,'worker_hook_cases':8,'existing_worker_cases':existing_cases,
          'static_bytes_native':int(dimensions[1]),'snapshot_max_bytes':int(dimensions[2]),
          'strict_asan_ubsan':True,'leak_detection':True,'source_stable':True,'source_sha256':after,
          'device_contact':False,'cloud_contact':False,'real_sd_operations':False,'commands':RECORD}
 except Exception:
  (OUT/'results.json').write_text(json.dumps({'pass':False,'commands':RECORD},indent=2)+'\n');raise
 (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({key:result[key] for key in ['pass','component_cases','worker_hook_cases','existing_worker_cases','static_bytes_native','snapshot_max_bytes']},indent=2))
if __name__=='__main__':main()
