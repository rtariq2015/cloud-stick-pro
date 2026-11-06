"""Exact lab1 versus corrected source clock regression; no SDK/device/network."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/public-wifi-clock';OUT.mkdir(parents=True,exist_ok=True)
BASE='212090dcf7eef454674ec24e50f9d0abf7059ed5'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
FLAGS=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Iinclude','-Ilib/wifi_setup_model','-isystem',str(JSON)]
ENABLED=['-DCSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-DCSP_ENABLE_BACKUP_SETUP=1']
RUN=[]
def run(name,command,negative=None):
 result=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
 text=result.stdout+result.stderr;(OUT/(name+'.log')).write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 passed=result.returncode!=0 and negative in text and not diagnostic if negative else result.returncode==0 and not result.stderr and not diagnostic
 RUN.append({'name':name,'command':list(map(str,command)),'exit':result.returncode,'stderr_empty':not result.stderr,'expected_old_failure':bool(negative),'sanitizer_diagnostic':diagnostic,'passed':passed,'log_sha256':hashlib.sha256(text.encode()).hexdigest()})
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:raise RuntimeError(name+' failed; inspect '+str(OUT/(name+'.log')))
def build_runtime(name,runtime,policy,clock=True):
 binary=OUT/name
 flags=['-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),'-include',
        'test/public_wifi_assistance/clock_runtime_stubs.h' if clock else 'test/backup_integration/runtime_stubs.h','-pthread',
        '-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_USB_BENCH_AUTOMATION=1','-DCONFIG_BT_NIMBLE_ENABLED=1',
        '-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
 run(name+'-build',[CLANG,*FLAGS,*ENABLED,*flags,'test/public_wifi_assistance/runtime.test.cpp',runtime,'src/wifi_setup_nvs.cpp',policy,
       'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp','-o',binary])
 return binary
def main():
 sources=['src/public_wifi_assistance.cpp','src/wifi_setup_runtime.cpp','test/public_wifi_assistance/clock_policy.test.cpp','test/public_wifi_assistance/clock_runtime_stubs.h','test/public_wifi_assistance/runtime.test.cpp']
 before={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
 for name in ['public_wifi_assistance.cpp','wifi_setup_runtime.cpp']:
  old=subprocess.run(['git','show',BASE+':src/'+name],cwd=ROOT,capture_output=True,check=True).stdout
  (OUT/('lab1-'+name)).write_bytes(old)
 fixture_source=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text();assert fixture_source.count('\nint main(')==1
 (OUT/'runtime_fixture_prefix.h').write_text(fixture_source.split('\nint main(',1)[0]+'\n')
 sys.path.insert(0,str(ROOT/'tools'));from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
 owner=OUT/'synthetic-owner.bin';owner.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
 oldpolicy=OUT/'lab1-public_wifi_assistance.cpp';oldruntime=OUT/'lab1-wifi_setup_runtime.cpp'
 try:
  run('lab1-policy-build',[CLANG,*FLAGS,*ENABLED,oldpolicy,'test/public_wifi_assistance/clock_policy.test.cpp','-o',OUT/'lab1-policy'])
  run('lab1-stale-policy-reproduction',[OUT/'lab1-policy'],negative='stale.remaining_ms==duration')
  binary=build_runtime('lab1-advancing-runtime',oldruntime,oldpolicy)
  run('lab1-owner-clock-reproduction',[binary,owner,'normal'],negative='Clock regression: after begin phase=complete error=5')
  # Prove each correction independently: fresh owner sampling protects the real
  # runtime even with old policy; defensive policy protects a stale owner sample.
  runtime_only=build_runtime('fresh-runtime-old-policy','src/wifi_setup_runtime.cpp',oldpolicy)
  run('fresh-owner-sample-alone',[runtime_only,owner,'normal'])
  policy_only=build_runtime('old-runtime-clock-guard',oldruntime,'src/public_wifi_assistance.cpp')
  run('guard-alone-with-stale-owner',[policy_only,owner,'normal'])
  run('fixed-policy-build',[CLANG,*FLAGS,*ENABLED,'src/public_wifi_assistance.cpp','test/public_wifi_assistance/clock_policy.test.cpp','-o',OUT/'fixed-policy'])
  run('fixed-clock-policy',[OUT/'fixed-policy'])
  fixed=build_runtime('fixed-advancing-runtime','src/wifi_setup_runtime.cpp','src/public_wifi_assistance.cpp')
  for case in ['normal','cancel','not-selected','prepare-expiry','terminal','route-start-fail','route-cleanup-fail','probe-fail','probe-timeout','memory-low']:
   run('fixed-advancing-'+case,[fixed,owner,case])
  stable=before=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
  assert stable,'Source changed during clock gate'
  result={'passed':True,'baseline_commit':BASE,'source_sha256':before,'sources_unchanged':stable,'desktop_only':True,'device_contact':False,'records':RUN}
 except Exception:
  (OUT/'results.json').write_text(json.dumps({'passed':False,'records':RUN},indent=2)+'\n');raise
 (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
