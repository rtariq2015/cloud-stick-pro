"""BLE UART window clock regressions; synthetic native runtime, no device/SDK."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/ble-window-clock';OUT.mkdir(parents=True,exist_ok=True)
BASE='076180b7630f157b770f7716ae9400bf50ae33c9'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
FLAGS=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Ilib/wifi_setup_model','-isystem',str(JSON)]
RECORD=[]
def run(name,command,negative=None):
 result=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
 output=result.stdout+result.stderr;log=OUT/(name+'.log');log.write_text(output)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
 passed=result.returncode!=0 and negative in output and not diagnostic if negative else result.returncode==0 and not result.stderr and not diagnostic
 RECORD.append({'name':name,'command':list(map(str,command)),'exit':result.returncode,'stderr_empty':not result.stderr,'expected_old_failure':bool(negative),'sanitizer_diagnostic':diagnostic,'passed':passed,'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:raise RuntimeError(name+' failed; inspect '+str(log))
def runtime(name,source,headers):
 binary=OUT/name
 flags=['-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),'-I'+str(headers),'-Iinclude',
        '-include','test/backup_integration/lifecycle_clock_stubs.h','-pthread','-DCSP_ENABLE_BACKUP_SETUP=1',
        '-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_USB_BENCH_AUTOMATION=1',
        '-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
 run(name+'-build',[CLANG,*FLAGS,*flags,'test/backup_integration/lifecycle_clock_runtime.test.cpp',source,'src/wifi_setup_nvs.cpp',
  'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp','-o',binary])
 return binary
def main():
 sources=['include/pro_ble_lifecycle.h','src/wifi_setup_runtime.cpp','test/ble_lifecycle_clock.test.cpp','test/backup_integration/lifecycle_clock_stubs.h','test/backup_integration/lifecycle_clock_runtime.test.cpp']
 before={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources}
 baseline=OUT/'baseline';baseline.mkdir(exist_ok=True)
 for name in ['include/pro_ble_lifecycle.h','src/wifi_setup_runtime.cpp']:
  source=subprocess.run(['git','show',BASE+':'+name],cwd=ROOT,capture_output=True,check=True).stdout
  (baseline/Path(name).name).write_bytes(source)
 fixture_source=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text();assert fixture_source.count('\nint main(')==1
 (OUT/'runtime_fixture_prefix.h').write_text(fixture_source.split('\nint main(',1)[0]+'\n')
 sys.path.insert(0,str(ROOT/'tools'));from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
 owner=OUT/'synthetic-owner.bin';owner.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
 try:
  run('baseline-policy-build',[CLANG,*FLAGS,'-I'+str(baseline),'-Iinclude','test/ble_lifecycle_clock.test.cpp','-o',OUT/'baseline-policy'])
  run('baseline-stale-window-reproduction',[OUT/'baseline-policy'],negative='gate.poll(999,true,false,500000,250000)==Action::None')
  old=runtime('baseline-uart-runtime',baseline/'wifi_setup_runtime.cpp',baseline)
  run('baseline-uart-window-reproduction',[old,owner,'normal'],negative='Clock regression: UART window became blocked before valid late reap')
  fresh=runtime('fresh-poll-old-policy','src/wifi_setup_runtime.cpp',baseline)
  run('fresh-poll-alone',[fresh,owner,'normal'])
  # A quoted include searches the source directory before -I. Keep this copy
  # outside baseline/, otherwise its colocated old header would mask the guard.
  old_runtime_with_new_header=OUT/'old-runtime-current-header.cpp'
  old_runtime_with_new_header.write_bytes((baseline/'wifi_setup_runtime.cpp').read_bytes())
  guarded=runtime('old-poll-guarded-policy',old_runtime_with_new_header,ROOT/'include')
  run('stale-age-guard-alone',[guarded,owner,'normal'])
  run('fixed-policy-build',[CLANG,*FLAGS,'-Iinclude','test/ble_lifecycle_clock.test.cpp','-o',OUT/'fixed-policy'])
  run('fixed-clock-policy',[OUT/'fixed-policy'])
  fixed=runtime('fixed-uart-runtime','src/wifi_setup_runtime.cpp',ROOT/'include')
  for scenario in ['normal','wrap','repeat','timeout']:run('fixed-uart-'+scenario,[fixed,owner,scenario])
  run('existing-lifecycle-build',[CLANG,*FLAGS,'-Iinclude','test/ble_lifecycle.test.cpp','-o',OUT/'existing-policy'])
  run('existing-lifecycle',[OUT/'existing-policy'])
  after={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sources};assert before==after
  result={'passed':True,'desktop_only':True,'baseline_commit':BASE,'sources_unchanged':True,'source_sha256':after,
   'policy_clock_groups':5,'actual_uart_scenarios':4,'same_boot_cycles':3,'real_timeout_stays_blocked':True,
   'device_contact':False,'sdk_build':False,'records':RECORD}
 except Exception:
  (OUT/'results.json').write_text(json.dumps({'passed':False,'records':RECORD},indent=2)+'\n');raise
 (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
