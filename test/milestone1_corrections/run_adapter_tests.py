"""Run strict host regressions. Every subprocess must exit cleanly and have empty stderr."""
from pathlib import Path
import hashlib,json,os,subprocess
root=Path(__file__).resolve().parents[2]
out=root/'artifacts/milestone1-corrections/adapter'
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
timers=['stale-connect','future-connect','future-info','timeout-wrap','timeout-new-generation','timeout-refreshed-generation','absolute-window']
boundaries=['new-session','handshake','decrypt','encrypt','info','replaced-session']
records=[]
for binary,cases,logname in [('timer-tests',timers,'timer-tests.log'),('callback-tests',boundaries,'callback-tests.log')]:
 lines=[]
 for case in cases:
  cmd=[str(out/binary),case];result=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=10)
  lines.append(result.stdout);record={'case':case,'command':cmd,'exit_code':result.returncode,'stderr_empty':not result.stderr}
  records.append(record)
  if result.returncode or result.stderr:
   (out/logname).write_text(''.join(lines)+result.stderr)
   raise SystemExit('FAIL '+case+': '+result.stderr)
 (out/logname).write_text(''.join(lines))
result=subprocess.run(['node','--test',str(root/'test/milestone1_corrections/reject_latch.test.mjs')],
 capture_output=True,text=True,env=env,timeout=90)
(out/'reject-latch.log').write_text(result.stdout+result.stderr)
if result.returncode or result.stderr:raise SystemExit('FAIL actual adapter/SDK latch tests: '+result.stdout+result.stderr)
sources=['src/ble_management.cpp','include/ble_management.h','include/pro_ble_message.h','sdk/pinned_ble/patched/security2.c']
report={'pass':True,'timer_cases':len(timers),'callback_boundary_cases':len(boundaries),'actual_sdk_latch_cases':5,
 'records':records,'sdk_test_log':'reject-latch.log','strict_sanitizers':'ASan+UBSan halt_on_error; leak detection; nonempty stderr always fails',
 'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in sources},
 'scope':'actual firmware adapter, actual pinned SDK SRP/GCM with reviewed IV alignment correction; lifecycle/radio/clock/heap/MAC stubbed; no physical-device evidence'}
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS 7 timing + 6 response-boundary + 5 actual SDK latch cases; strict ASan/UBSan and empty stderr')
