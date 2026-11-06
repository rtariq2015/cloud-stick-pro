"""Strict native actual-publisher tests; synthetic DAV only, all file opens denied."""
from pathlib import Path
import os,json,hashlib,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/remote-health-publisher'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def main():
 OUT.mkdir(parents=True,exist_ok=True);records=[]
 inputs=['src/remote_health_publisher.cpp','include/remote_health_publisher.h','include/webdav_client.h','src/webdav_client.cpp','src/remote_health_model.cpp','include/remote_health_model.h','src/pro_core.cpp','include/http_response_guard.h']
 inputs += ['include/pro_core.h','include/pro_device.h','include/config_loader.h','include/ble_feature.h',
            'include/pro_dav_properties.h','include/usb_storage.h','lib/tinyxml2/src/tinyxml2.h','lib/tinyxml2/src/tinyxml2.cpp']
 inputs += [str(p.relative_to(ROOT)) for p in (ROOT/'test/backup_integration/dav_stubs').rglob('*') if p.is_file()]
 inputs += [str(p.relative_to(ROOT)) for p in (ROOT/'test/remote_health_publisher').rglob('*') if p.is_file()]
 hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in inputs}
 def run(name,args):
  p=subprocess.run(args,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180);output=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(output)
  diag=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output));secret=any(s in output for s in ['synthetic-user','synthetic-secret','synthetic-ca'])
  passed=p.returncode==0 and not p.stderr and not diag and not secret
  records.append(dict(name=name,command=args,exit=p.returncode,stderr_empty=not p.stderr,sanitizer_diagnostic=diag,credentials_in_output=secret,passed=passed))
  (OUT/'results.json').write_text(json.dumps(dict(passed=all(r['passed'] for r in records),desktop_only=True,real_network=False,device_access=False,actual_sources=inputs[:7],source_sha256=hashes,records=records),indent=2)+'\n')
  print(('PASS ' if passed else 'FAIL ')+name,flush=True)
  if not passed:print(output[-6000:],file=sys.stderr);raise SystemExit(1)
  return p.stdout
 flags=[str(CLANG),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Itest/remote_health_publisher/stubs','-Itest/backup_integration/dav_stubs','-Iinclude','-Ilib/tinyxml2/src']
 combined=['-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1']
 strict=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
 binary=OUT/'publisher-tests'
 run('strict-build',flags+strict+combined+['-DCSP_ENABLE_REMOTE_HEALTH=1','src/remote_health_publisher.cpp','src/webdav_client.cpp','src/remote_health_model.cpp','src/pro_core.cpp','lib/tinyxml2/src/tinyxml2.cpp','test/remote_health_publisher/publisher.test.cpp','-lcrypto','-Wl,--wrap=fopen','-o',str(binary)])
 run('strict-tests',[str(binary)])
 for name,feature in [('default',[]),('disabled',['-DCSP_ENABLE_REMOTE_HEALTH=0']),('enabled',['-DCSP_ENABLE_REMOTE_HEALTH=1'])]:
  obj=OUT/(name+'.o');run(name+'-object',flags+combined+feature+['-fstack-usage','-c','src/remote_health_publisher.cpp','-o',str(obj)])
  symbols=run(name+'-symbols',['nm','-C',str(obj)])
  if name!='enabled':assert 'pro::health' not in symbols and b'CSP-HEALTH' not in obj.read_bytes() and not run(name+'-undefined',['nm','-u',str(obj)]).strip()
  run(name+'-api',flags+combined+feature+['-c','test/remote_health_publisher/api.test.cpp','-o',str(OUT/(name+'-api.o'))])
 run('object-sizes',['size',str(OUT/'enabled.o'),str(OUT/'default.o'),str(OUT/'disabled.o')])
 assert all(hashlib.sha256((ROOT/f).read_bytes()).hexdigest()==h for f,h in hashes.items()),'Publisher changed during gate; rerun after freeze'
 run('provider201-policy',[sys.executable,str(ROOT/'test/remote_health_publisher/run_status201.py')])
 print('PASS source hashes unchanged; no real cloud, files or device used',flush=True)
if __name__=='__main__':main()
