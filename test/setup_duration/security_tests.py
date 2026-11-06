"""Reuse the pinned real Security2 host/negative suite with guided policy enabled."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/setup-duration/security';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root')
env=dict(os.environ,CPATH=str(DEPS/'usr/include'),LIBRARY_PATH=str(DEPS/'usr/lib/x86_64-linux-gnu'),LD_LIBRARY_PATH=str(DEPS/'usr/lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
records=[]
def run(name,command):
 p=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
 (OUT/(name+'.stdout.log')).write_text(p.stdout);(OUT/(name+'.stderr.log')).write_text(p.stderr)
 inspected=p.stdout;fixture_titles=0
 if name=='security2-negative-tests':
  # Four existing harness tests deliberately PRINT the diagnostic fixture in
  # their successful test name. Remove only those exact title lines, not output
  # from SDK children (the harness itself drains and rejects every child stderr).
  pattern=r'^✔ host cleanup rejects late stderr with zero exit \(cleanup called (?:before|after) exit\): (?:runtime error: late alignment diagnostic|ERROR: AddressSanitizer: late diagnostic) \([0-9.]+ms\)\n'
  inspected,fixture_titles=re.subn(pattern,'',inspected,flags=re.MULTILINE)
 passed=p.returncode==0 and not p.stderr and not re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:',inspected) and (name!='security2-negative-tests' or fixture_titles==4)
 records.append({'name':name,'command':command,'exit_code':p.returncode,'stderr_empty':not p.stderr,'synthetic_harness_title_lines':fixture_titles,'passed':bool(passed)})
 if not passed:raise RuntimeError(name+' failed; preserved logs contain details')
 return p.stdout
run('sdk-provenance',['python3','tools/pinned_ble_sdk.py'])
build=json.loads((ROOT/'artifacts/milestone1-corrections/adapter/build.json').read_text())
commands=[c for c in build['commands'] if any(str(x).endswith('actual_adapter_sdk.cpp')for x in c)]
assert len(commands)==1
command=list(commands[0]);command.insert(1,'-DCSP_PRODUCT_SETUP_DEV_PROFILE=1');command[command.index('-o')+1]=str(OUT/'guided-actual-sdk-host')
run('build',command)
env['CSP_ADAPTER_HOST']=str(OUT/'guided-actual-sdk-host')
env['CSP_GUIDED_SESSION']='1'
stdout=run('security2-negative-tests',['node','--test','--test-reporter=spec','test/milestone1_corrections/reject_latch.test.mjs'])
match=re.search(r'^(?:#|ℹ) tests (\d+)$',stdout,re.MULTILINE)
report={'passed':True,'node_tests':int(match.group(1))if match else None,'records':records,'source_sha256':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest()for f in ['src/ble_management.cpp','include/pro_setup_session.h','sdk/pinned_ble/patched/security2.c','test/milestone1_corrections/actual_adapter_sdk.cpp','test/setup_duration/security_tests.py']},'scope':'Actual Security2 SRP/GCM C and actual adapter with guided policy; synthetic credentials, stubbed clock/radio/heap; no physical-device acceptance','hardware_contact':False}
(OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS guided actual-SDK Security2 and negative suite',report['node_tests'])
