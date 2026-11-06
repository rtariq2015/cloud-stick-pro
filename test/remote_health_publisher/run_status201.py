"""Strict synthetic diagnosis of RC4 publisher201 and retained retry behavior."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/remote-health-status201-reproduction'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
 ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
 UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
INPUTS=['src/remote_health_publisher.cpp','src/webdav_client.cpp','src/remote_health_worker.cpp',
 'src/remote_health_model.cpp','src/pro_core.cpp','src/sync_engine.cpp',
 'include/remote_health_model.h','include/remote_health_worker.h','include/remote_health_publisher.h',
 'test/remote_health_publisher/publisher.test.cpp','test/remote_health_publisher/status201.test.cpp',
 'test/remote_health_publisher/status_compatibility.test.cpp',
 'test/remote_health_worker/retained_failure.test.cpp','test/remote_health_publisher/run_status201.py']
for folder in ['test/remote_health_publisher/stubs','test/backup_integration/dav_stubs','test/backup_integration/stubs']:
 INPUTS.extend(str(p.relative_to(ROOT)) for p in (ROOT/folder).rglob('*') if p.is_file())

def main():
 OUT.mkdir(parents=True,exist_ok=True)
 hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in INPUTS}
 records=[]
 def run(name,args):
  p=subprocess.run(args,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
  output=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(output)
  diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
  secret=any(s in output for s in ['synthetic-user','synthetic-secret','synthetic-ca'])
  passed=p.returncode==0 and not p.stderr and not diagnostic and not secret
  records.append(dict(name=name,command=args,exit=p.returncode,stderr_empty=not p.stderr,
   sanitizer_diagnostic=diagnostic,synthetic_secret_in_output=secret,passed=passed))
  (OUT/'results.json').write_text(json.dumps(dict(passed=all(r['passed'] for r in records),
   baseline_commit='d4832711e241cf076a7ce2fa736106ee9fd4221d',production_source_changed=True,
   actual_publisher_worker_model=True,desktop_only=True,real_network=False,device_access=False,
   source_sha256=hashes,records=records),indent=2)+'\n')
  print(('PASS ' if passed else 'FAIL ')+name,flush=True)
  if not passed:print(output[-6000:],file=sys.stderr);raise SystemExit(1)
 flags=[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',
  '-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
  '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_REMOTE_HEALTH=1']
 publisher=OUT/'status201-tests'
 run('publisher-build',flags+['-Itest/remote_health_publisher/stubs','-Itest/backup_integration/dav_stubs',
  '-Iinclude','-Ilib/tinyxml2/src','src/remote_health_publisher.cpp','src/webdav_client.cpp','src/remote_health_model.cpp',
  'src/pro_core.cpp','lib/tinyxml2/src/tinyxml2.cpp','test/remote_health_publisher/status201.test.cpp',
  '-lcrypto','-Wl,--wrap=fopen','-o',str(publisher)])
 run('publisher201-reproduction',[str(publisher)])
 compatible=OUT/'status-compatibility-tests'
 run('compatibility-build',flags+['-Itest/remote_health_publisher/stubs','-Itest/backup_integration/dav_stubs',
  '-Iinclude','-Ilib/tinyxml2/src','src/remote_health_publisher.cpp','src/webdav_client.cpp','src/remote_health_model.cpp',
  'src/pro_core.cpp','lib/tinyxml2/src/tinyxml2.cpp','test/remote_health_publisher/status_compatibility.test.cpp',
  '-lcrypto','-Wl,--wrap=fopen','-o',str(compatible)])
 run('provider-compatibility',[str(compatible)])
 worker=OUT/'retained-retry-tests'
 run('retained-worker-build',flags+['-pthread','-Itest/backup_integration/stubs','-Iinclude',
  'src/remote_health_worker.cpp','src/remote_health_model.cpp','test/remote_health_worker/retained_failure.test.cpp',
  '-Wl,--wrap=time','-o',str(worker)])
 run('retained-worker-reproduction',[str(worker)])
 assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==digest for p,digest in hashes.items())
 result=json.loads((OUT/'results.json').read_text());result['source_unchanged_during_tests']=True
 (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':main()
