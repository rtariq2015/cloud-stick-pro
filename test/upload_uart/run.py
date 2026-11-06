"""Actual owner-loop UPLOAD_STATUS roundtrip; synthetic snapshots/transport only."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/upload-uart';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
RECORD=[]
def run(name,command):
 result=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
 text=result.stdout+result.stderr;path=OUT/(name+'.log');path.write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 passed=result.returncode==0 and not result.stderr and not diagnostic
 RECORD.append({'name':name,'command':list(map(str,command)),'exit':result.returncode,'stderr_empty':not result.stderr,'sanitizer_diagnostic':diagnostic,'passed':passed,'log_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:raise RuntimeError(name+' failed; inspect '+str(path))
 return text
def main():
 tracked=['src/wifi_setup_runtime.cpp','include/upload_diagnostics.h','test/upload_uart/runtime.test.cpp']
 before={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in tracked}
 prefix=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text();assert prefix.count('\nint main(')==1
 (OUT/'runtime_fixture_prefix.h').write_text(prefix.split('\nint main(',1)[0]+'\n')
 sys.path.insert(0,str(ROOT/'tools'));from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
 owner=OUT/'synthetic-owner.bin';owner.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
 flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
  '-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),'-Iinclude','-Ilib/wifi_setup_model','-isystem',str(JSON),'-include','test/backup_integration/runtime_stubs.h','-pthread',
  '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_USB_BENCH_AUTOMATION=1',
  '-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
 counts={}
 try:
  for mode in ['default','disabled','enabled']:
   feature=[] if mode=='default' else ['-DCSP_UPLOAD_DIAGNOSTICS='+('1' if mode=='enabled' else '0')]
   binary=OUT/('runtime-'+mode)
   run(mode+'-build',[CLANG,*flags,*feature,'test/upload_uart/runtime.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
    'lib/wifi_setup_model/wifi_setup_model.cpp','lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp','lib/wifi_setup_model/owner_record.cpp','-o',binary])
   output=run(mode+'-uart',[binary,owner]);counts[mode]=int(re.search(r'cases=(\d+)',output)[1])
  after={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in tracked};assert before==after,'Touched UART source changed during tests'
  value={'passed':True,'desktop_only':True,'device_contact':False,'sdk_build':False,'source_sha256':after,'sources_unchanged':True,
   'fields':38,'case_counts':counts,'scope':'Actual UART handler/owner loop with synthetic accessor/USB/radio/NVS/worker; no actual TLS or cloud', 'records':RECORD}
 except Exception:
  (OUT/'results.json').write_text(json.dumps({'passed':False,'records':RECORD},indent=2)+'\n');raise
 (OUT/'results.json').write_text(json.dumps(value,indent=2)+'\n')
if __name__=='__main__':main()
