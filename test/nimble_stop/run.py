"""Actual old/new NPL bodies with deterministic logical cross-core effects."""
from pathlib import Path
import hashlib,importlib.util,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/nimble-stop-repair';OUT.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('pinned_stop',ROOT/'tools/pinned_nimble_stop.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr');CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def source_hashes():
 paths=[ROOT/'tools/pinned_nimble_stop.py',ROOT/'src/ble_sdk_host_stop.c',ROOT/'src/ble_sdk_npl_stop.c',ROOT/'include/nimble_stop_repair.h']
 paths.extend(p for folder in ['sdk/pinned_nimble_stop','test/nimble_stop'] for p in (ROOT/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
 return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}
before=source_hashes()
records=[]
def run(name,command,expected_assert=None,expected_empty=False):
 p=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=60)
 text=p.stdout+p.stderr;log=OUT/(name+'.log');log.write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 passed=(p.returncode!=0 and expected_assert in text and not diagnostic) if expected_assert else (p.returncode==0 and not p.stderr and not diagnostic and (not expected_empty or not p.stdout))
 records.append({'name':name,'command':list(map(str,command)),'exit':p.returncode,'passed':passed,'expected_assertion':expected_assert,'sanitizer_diagnostic':diagnostic,'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
 (OUT/'results.json').write_text(json.dumps({'passed':all(x['passed'] for x in records),'records':records},indent=2)+'\n')
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:raise RuntimeError(text)
for variant in ['upstream','patched']:
 directory=OUT/variant;directory.mkdir(exist_ok=True)
 npl=(m.BASE/variant/'npl_os_freertos.c').read_text();hs=(m.BASE/variant/'ble_hs.c').read_text()
 names=['in_isr','npl_freertos_eventq_get','npl_freertos_eventq_put','npl_freertos_eventq_remove','npl_freertos_event_is_queued','npl_freertos_event_deinit','npl_freertos_callout_stop','npl_freertos_callout_deinit']
 if variant=='patched':
  names[1:1]=['npl_eventq_lock','npl_eventq_unlock']
  names.extend(['npl_freertos_hw_enter_critical','npl_freertos_hw_is_in_critical','npl_freertos_hw_exit_critical'])
 (directory/'queue_bodies.h').write_text('\n\n'.join(m.function(npl,n) for n in names)+'\n')
 (directory/'timer_body.h').write_text(m.function(hs,'ble_hs_timer_reset')+'\n')
 flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(directory)]
 run(variant+'-build',[CLANG,*flags,*(['-DFIXED_VARIANT=1'] if variant=='patched' else []),'test/nimble_stop/npl.test.cpp','-o',directory/'test'])
 if variant=='upstream':
  for mode,assertion in [('snapshot','ret == pdPASS'),('producer','ret == pdPASS'),('timer-held','!calloutFreed')]:run('old-'+mode,[directory/'test',mode],expected_assert=assertion)
 else:
  for mode in ['snapshot','snapshot-isr','producer','producer-isr','duplicate','nonqueued','deinitialized-query','run-unlocked','timeout','wrap','stolen-wake','mutex-timeout','timer-held','repeated','critical-put-remove','critical-depth']:
   run('fixed-'+mode,[directory/'test',mode])
  for mode in ['genuine-full','genuine-requeue']:run('fixed-'+mode,[directory/'test',mode],expected_assert='ret == pdPASS')
  run('fixed-wrong-queue',[directory/'test','wrong-queue'],expected_assert='removed')
  run('fixed-critical-blocking-get',[directory/'test','critical-blocking-get'],expected_assert='tmo == 0 || hw_critical_state_status[xPortGetCoreID()] == 0')
run('pinned-generator',['python3','tools/pinned_nimble_stop.py'])
directory=OUT/'patched'
npl=(m.BASE/'patched/npl_os_freertos.c').read_text()
# Pool functions use a different one-line declaration spelling in the SDK.
def pool_function(name):
 text=npl.replace('int '+name+'(', 'int\n'+name+'(').replace('void '+name+'(', 'void\n'+name+'(')
 return m.function(text,name)
(directory/'pool_bodies.h').write_text('\n\n'.join(pool_function(n) for n in ['npl_freertos_mempool_init','npl_freertos_mempool_deinit'])+'\n')
run('pool-build',[CLANG,*flags,'test/nimble_stop/pool.test.cpp','-o',directory/'pool-test'])
for mode in ['mutex-allocation-failure','repeated-cleanup']:run('pool-'+mode,[directory/'pool-test',mode])
for module in ['ble_sdk_host_stop','ble_sdk_npl_stop']:
 target=directory/(module+'-off.o')
 run(module+'-default-off',[CLANG,'-x','c','-std=c11','-Wall','-Wextra','-Werror','-Iinclude','-c','src/'+module+'.c','-o',target])
 run(module+'-default-off-symbols',['nm',target],expected_empty=True)
policy=directory/'policy.c';policy.write_text('#include "nimble_stop_repair.h"\n')
for name,extra in [('disabled-management',[]),('wrong-profile',['-DCSP_ENABLE_BLE_MANAGEMENT=1'])]:
 run('reject-'+name,[CLANG,'-x','c','-std=c11','-Iinclude','-DCSP_NIMBLE_STOP_REPAIR=1',*extra,'-fsyntax-only',policy],expected_assert='NimBLE stop repair requires the explicit BLE-enabled lab5 or release-candidate profile')
assert before==source_hashes(),'Source changed during native tests'
result={'passed':True,'desktop_only':True,'device_contact':False,'real_SMP_or_ISR_timing':False,'old_assertions_reproduced':3,'genuine_invariant_assertions_retained':2,'additional_invariant_rejections':2,'repeated_timer_cycles':32,'pool_lifecycle_cycles':64,'source_stable':True,'source_sha256':before,'target_provider_ownership_pending':True,'records':records}
(OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
