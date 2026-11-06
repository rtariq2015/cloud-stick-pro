"""Actual production poll/callback source; strict sanitizers, synthetic SDK only."""
from pathlib import Path
import hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/setup-duration/management';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root')
CXX=DEPS/'usr/lib/llvm-18/bin/clang++'
env=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'usr/lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
defines=['CSP_ENABLE_BLE_MANAGEMENT=1','CSP_BLE_SYNTHETIC_ONLY=1','CSP_BLE_ISOLATED_TEST=1','CONFIG_BT_NIMBLE_ENABLED=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1','CSP_PRODUCT_SETUP_DEV_PROFILE=1']
cmd=[str(CXX),'-std=c++17','-pthread','-Wall','-Wextra','-Werror','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',*[f'-D{x}'for x in defines],'-Itest/milestone1_corrections/adapter_stubs','-Iinclude','test/setup_duration/management.test.cpp','-o',str(OUT/'management-tests')]
records=[]
def run(name,command):
 p=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=90)
 (OUT/(name+'.stdout.log')).write_text(p.stdout);(OUT/(name+'.stderr.log')).write_text(p.stderr)
 record={'name':name,'command':command,'exit_code':p.returncode,'stderr_empty':not p.stderr,'passed':p.returncode==0 and not p.stderr and not any(t in p.stdout for t in ['AddressSanitizer','UndefinedBehaviorSanitizer','runtime error:'])};records.append(record)
 if not record['passed']:raise RuntimeError(name+' failed; see preserved logs')
run('build',cmd)
cases=['unauthenticated-window','late-session-admission','handshake-exact','late-handshake-admission','authenticated-past-window','idle-exact','valid-activity-refresh','late-activity-denied','stale-poll-refresh','stale-poll-authentication','late-poll-authentication-denied','disconnect-prompt','disconnect-terminal','unrelated-close','invalid-frame-terminal','low-memory-preserved','idle-wrap','advertising-wrap']
for case in cases:run(case,[str(OUT/'management-tests'),case])
inputs=['src/ble_management.cpp','include/pro_ble_lifecycle.h','include/pro_setup_session.h','src/wifi_setup_runtime.cpp','src/main.cpp','test/setup_duration/management.test.cpp','test/setup_duration/management_tests.py']
report={'passed':True,'cases':len(cases),'records':records,'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p in inputs},'scope':'Actual production adapter callbacks/poll with synthetic SDK lifecycle, crypto, clock, heap and MAC; no physical evidence or claim of a real authenticated handshake','hardware_contact':False}
(OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS',len(cases),'production timing cases; strict ASan/UBSan and empty stderr')
