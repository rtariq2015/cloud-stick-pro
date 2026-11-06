#!/usr/bin/env python3
"""Bounded native actual receipt/cache/runtime/worker gates; synthetic inputs only."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/file-history'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
FLAGS=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-pthread','-Iinclude']
COMBINED=['-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1']
RECORD=[]
def run(name,command,rejection=None):
 r=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
 out=r.stdout+r.stderr;log=OUT/(name+'.log');log.write_text(out)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',out))
 passed=r.returncode!=0 and rejection in r.stderr if rejection else r.returncode==0 and not r.stderr and not diagnostic
 RECORD.append(dict(name=name,command=list(map(str,command)),exit=r.returncode,stderr_empty=not r.stderr,sanitizer_diagnostic=diagnostic,expected_compile_rejection=rejection,passed=passed,log=str(log.relative_to(ROOT)),log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
 (OUT/'results.json').write_text(json.dumps(dict(passed=all(x['passed'] for x in RECORD),records=RECORD),indent=2)+'\n')
 print(('PASS' if passed else 'FAIL'),name,flush=True)
 if not passed:raise RuntimeError(out)
 return r.stdout
def hashes():
 paths=[*ROOT.glob('test/file_history/*'),ROOT/'include/file_history.h',ROOT/'src/file_history.cpp',ROOT/'src/sync_engine.cpp',ROOT/'src/wifi_setup_runtime.cpp',ROOT/'src/ble_management.cpp',ROOT/'lib/wifi_setup_model/wifi_wire.h',ROOT/'lib/wifi_setup_model/wifi_wire.cpp']
 return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths) if p.is_file()}
def main():
 OUT.mkdir(parents=True,exist_ok=True);before=hashes()
 with tempfile.TemporaryDirectory(prefix='csp-file-history-native-') as tmp:
  base=Path(tmp)
  run('model-build',[CLANG,*FLAGS,*COMBINED,'-DCSP_ENABLE_FILE_HISTORY=1','-DCSP_FILE_HISTORY_TESTING=1','-include','test/file_history/alloc.h','src/file_history.cpp','src/pro_journal.cpp','src/pro_core.cpp','test/file_history/model.test.cpp','-Wl,--wrap=fopen','-o',OUT/'model-tests'])
  model=run('model-tests',[OUT/'model-tests',base/'model'])
  for mode,defines in [('default',[]),('off',[*COMBINED,'-DCSP_ENABLE_FILE_HISTORY=0']),('without-backup',['-DCSP_ENABLE_FILE_HISTORY=1','-DCSP_ENABLE_BACKUP_SETUP=0']),('enabled',[*COMBINED,'-DCSP_ENABLE_FILE_HISTORY=1']),('with-events',[*COMBINED,'-DCSP_ENABLE_FILE_HISTORY=1','-DCSP_ENABLE_EVENT_LOG=1'])]:
   binary=OUT/('wire-'+mode)
   run('wire-'+mode+'-build',[CLANG,*FLAGS,'-Ilib/wifi_setup_model','-isystem',JSON,*defines,'lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','test/file_history/wire.test.cpp','-o',binary])
   run('wire-'+mode,[binary])
  run('default-off-object',[CLANG,'-std=c++17','-Iinclude','-c','src/file_history.cpp','-o',OUT/'default-off.o'])
  assert not run('default-off-symbols',['nm','--defined-only',OUT/'default-off.o']).strip()
  for flags,message,name in [(['-DCSP_ENABLE_FILE_HISTORY=2'],'must be zero or one','invalid-flag'),(['-DCSP_ENABLE_FILE_HISTORY=1'],'requires the owner-authenticated','without-backup-object')]:
   run(name,[CLANG,'-std=c++17','-Iinclude',*flags,'-c','src/file_history.cpp','-o',OUT/'rejected.o'],rejection=message)
  for source,target in [('test/backup_integration/runtime_handoff.test.cpp','runtime_fixture_prefix.h'),('test/public_wifi_assistance/runtime.test.cpp','public_runtime_fixture_prefix.h')]:
   (OUT/target).write_text((ROOT/source).read_text().split('\nint main(',1)[0]+'\n')
  sys.path.insert(0,str(ROOT/'tools'));from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
  owner=OUT/'synthetic-owner.bin';owner.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
  runtime_flags=[*COMBINED,'-DCSP_FILE_HISTORY_TESTING=1','-DCSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-DCSP_USB_BENCH_AUTOMATION=1','-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1','-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),'-Ilib/wifi_setup_model','-isystem',str(JSON),'-include','test/backup_integration/runtime_stubs.h']
  for enabled in [0,1]:
   binary=OUT/('runtime-'+str(enabled))
   run('runtime-'+str(enabled)+'-build',[CLANG,*FLAGS,*runtime_flags,'-DCSP_ENABLE_FILE_HISTORY='+str(enabled),'-DCSP_ENABLE_EVENT_LOG='+str(enabled),'-DCSP_EVENT_LOG_ROOT="'+str(base/'runtime-events')+'"','test/file_history/runtime.test.cpp','src/file_history.cpp','src/pro_journal.cpp','src/pro_core.cpp','src/pro_event_log.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp','src/public_wifi_assistance.cpp','lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp','-Wl,--wrap=fopen','-o',binary])
   run('runtime-'+str(enabled),[binary,owner,base/('runtime'+str(enabled))])
  run('worker-build',[CLANG,'-Itest/backup_integration/stubs',*FLAGS,*COMBINED,'-DCSP_ENABLE_FILE_HISTORY=1','-Wno-mismatched-tags','src/sync_engine.cpp','test/file_history/worker.test.cpp','-o',OUT/'worker-tests'])
  for mode in ['normal','tls','snapshot','mount-fail','restart-fail']:run('worker-'+mode,[OUT/'worker-tests',mode])
  run('worker-with-events-build',[CLANG,'-Itest/backup_integration/stubs',*FLAGS,*COMBINED,'-DCSP_ENABLE_FILE_HISTORY=1','-DCSP_ENABLE_EVENT_LOG=1','-DCSP_EVENT_LOG_ROOT="'+str(base/'worker-events')+'"','-include','test/event_log/io.h','-Wno-mismatched-tags','src/sync_engine.cpp','src/pro_event_log.cpp','test/file_history/worker.test.cpp','-o',OUT/'worker-events'])
  for mode in ['normal','tls','snapshot','restart-fail']:run('worker-with-events-'+mode,[OUT/'worker-events',mode])
 after=hashes();assert before==after,'Source changed during test run'
 result=dict(passed=True,desktop_only=True,source_stable=True,source_sha256=after,strict_asan_ubsan=True,leak_detection=True,device_contact=False,cloud_contact=False,real_sd_operations=False,model=model,records=RECORD)
 (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS strict file-history native gates',flush=True)
if __name__=='__main__':main()
