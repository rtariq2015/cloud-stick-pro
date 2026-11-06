"""Actual parser/server/JS desktop tests. No device, saved config or cloud."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/local-dashboard-source'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1:allow_addr2line=1:external_symbolizer_path=/usr/bin/addr2line',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
OUT.mkdir(parents=True,exist_ok=True)
flags=['-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Iinclude',
 '-DCSP_ENABLE_LOCAL_DASHBOARD=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_PRODUCT_DASHBOARD_TRIAL=1',
 '-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1']
sources=['include/local_dashboard.h','src/local_dashboard.cpp','src/local_dashboard_model.cpp','src/local_dashboard_assets.cpp',
 'test/local_dashboard/model.test.cpp','test/local_dashboard/server.test.cpp','test/local_dashboard/passive_close.test.cpp',
 'test/local_dashboard/ui.test.mjs','test/local_dashboard/run.py']
hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}
records=[]
def publish(complete=False):
 stable=all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==value for p,value in hashes.items())
 passed=complete and stable and all(row['passed'] for row in records)
 (OUT/'results.json').write_text(json.dumps(dict(passed=passed,complete=complete,source_stable=stable,
  desktop_only=True,physical_device=False,cloud=False,source_sha256=hashes,records=records),indent=2)+'\n')
 return passed
def run(name,command):
 p=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=120)
 output=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(output)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
 passed=p.returncode==0 and not p.stderr and not diagnostic
 if passed and name.startswith('passive-close-') and not name.endswith('-build'):
  # Independent strict JSON decoding of actual TCP body, not a substring,
  # prefix, EOF-length guess or acceptance of an incomplete status object.
  expected=dict(firmware='0.4.0-dashboard1',uptime_ms=0,wifi=False,media=False,active=False,scanning=False,
   has_scan=True,scan_complete=True,window_open=True,paused=False,cloud=False,configured=False,
   verified=4,pending=0,failed=0,done=0,total=0,bytes_per_second=0,quiet_seconds=0,last_backup_unix=0,error=0,percent=-1,phase=0)
  try:
   lines=[line.removeprefix('NATIVE_STATUS_JSON ') for line in output.splitlines() if line.startswith('NATIVE_STATUS_JSON ')]
   assert len(lines)==1
   parsed=json.loads(lines[0]);assert isinstance(parsed,dict) and parsed.keys()==expected.keys()
   assert all(type(parsed[key]) is type(value) for key,value in expected.items()) and parsed==expected
  except (ValueError,AssertionError,KeyError,TypeError):passed=False
 records.append(dict(name=name,command=command,exit=p.returncode,sanitizer_diagnostic=diagnostic,passed=passed))
 publish()
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:print(output[-10000:]);raise SystemExit(1)
for passive in (0,1):
 for group,extra in [('model',[]),('server',['src/local_dashboard.cpp','src/local_dashboard_assets.cpp','-Wl,--wrap=close','-Wl,--wrap=_ZnwmRKSt9nothrow_t']),
     ('passive_close',['src/local_dashboard.cpp','src/local_dashboard_assets.cpp','-Wl,--wrap=close','-Wl,--wrap=recv','-Wl,--wrap=_ZnwmRKSt9nothrow_t','-Wl,--wrap=_ZdlPv'])]:
  name=('passive-close' if group=='passive_close' else group)+'-'+str(passive)
  binary=str(OUT/name)
  run(name+'-build',[str(DEPS/'lib/llvm-18/bin/clang++'),*flags,'-DCSP_DASHBOARD_PASSIVE_CLOSE='+str(passive),
   'src/local_dashboard_model.cpp',*extra,'test/local_dashboard/'+group+'.test.cpp','-o',binary])
  run(name,[binary])
run('phone-script',['node','--test','test/local_dashboard/ui.test.mjs'])
if not publish(complete=True):raise SystemExit('Source changed during tests; final result is FAIL')
