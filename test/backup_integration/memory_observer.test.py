"""Actual RAM observer, allocator failures/rate/wrap tests; no device access."""
from pathlib import Path
import json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/backup-wifi-integration/memory-observer';OUT.mkdir(exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
 UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
flags=['-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-pthread','-fsanitize=address,undefined',
 '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Itest/backup_integration/memory_stubs','-Iinclude']
compiler=str(DEPS/'lib/llvm-18/bin/clang++');records=[]
def run(name,args):
 r=subprocess.run(args,cwd=ROOT,env=env,capture_output=True,text=True,timeout=60)
 output=r.stdout+r.stderr;(OUT/(name+'.log')).write_text(output)
 passed=r.returncode==0 and not r.stderr and not re.search(r'AddressSanitizer:|LeakSanitizer:|runtime error:',output)
 records.append({'name':name,'passed':bool(passed),'exit':r.returncode,'stderr_empty':not r.stderr})
 assert passed,name+' failed; inspect retained log'
enabled=['-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1']
binary=str(OUT/'observer-test')
run('build',[compiler,*flags,*enabled,'src/backup_memory.cpp','test/backup_integration/memory_observer.test.cpp','-o',binary])
run('sampled-totals-wrap-rate-and-concurrent-failures',[binary])
run('registration-failure-explicit',[binary,'hook-failure'])
obj=str(OUT/'disabled.o');run('disabled-build',[compiler,*flags,'-c','src/backup_memory.cpp','-o',obj])
r=subprocess.run(['nm','-C',obj],capture_output=True,text=True,check=True);assert 'backup_memory_' not in r.stdout
(OUT/'results.json').write_text(json.dumps({'passed':True,'desktop_only':True,'default_off_symbols_absent':True,'records':records},indent=2)+'\n')
print('PASS memory observer: sampled totals, wrap/rate, 4000 concurrent failures, registration failure, default-off')
