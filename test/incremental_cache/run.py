#!/usr/bin/env python3
"""Strict synthetic native tests, including actual pinned FatFS; no device."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile,shutil,time
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/incremental-cache'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
SDK=Path('/config/.unmanic/research/cloud-stick-sdk-memory-core-20261104-asym-iram/packages')
FAT=SDK/'framework-espidf/components/fatfs/src'
CONFIG=SDK/'framework-arduinoespressif32-libs/esp32s3/qio_qspi/include'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 prior=list(OUT.glob('*.log'))+list(OUT.glob('results.json'))
 if prior:
  archive=OUT/'attempts'/time.strftime('%Y%m%dT%H%M%SZ',time.gmtime());archive.mkdir(parents=True,exist_ok=True)
  for path in prior:shutil.copy2(path,archive/path.name)
  (OUT/'results.json').unlink(missing_ok=True)
 names=['include/file_incremental_cache.h','include/pro_fat_layout.h','src/file_incremental_cache.cpp','include/pro_core.h','src/pro_core.cpp','include/usb_storage.h','src/usb_storage.cpp','src/sync_engine.cpp']
 files=[ROOT/n for n in names]+sorted((ROOT/'test/incremental_cache').rglob('*'))
 def hashes():return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()}
 before=hashes();records=[]
 def run(name,args):
  result=subprocess.run(list(map(str,args)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=120)
  output=result.stdout+result.stderr;(OUT/(name+'.log')).write_text(output)
  diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
  passed=result.returncode==0 and not result.stderr and not diagnostic
  records.append(dict(name=name,passed=passed,exit=result.returncode,command=list(map(str,args)),stderr_empty=not result.stderr,sanitizer_diagnostic=diagnostic))
  print(name,'PASS' if passed else 'FAIL',flush=True)
  if not passed:
   (OUT/'results.json').write_text(json.dumps(dict(schema='cloud-stick-incremental-cache-source-tests/v1',passed=False,desktop_only=True,source_sha256=before,records=records),indent=2)+'\n')
   raise RuntimeError(output[-14000:])
 cc=DEPS/'lib/llvm-18/bin/clang';cxx=DEPS/'lib/llvm-18/bin/clang++'
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-pthread','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 common=[cxx,'-std=c++17',*flags,'-Iinclude','src/pro_core.cpp','src/file_incremental_cache.cpp']
 run('default-build',[*common,'test/incremental_cache/default.test.cpp','-o',OUT/'default'])
 run('default',[OUT/'default'])
 run('model-build',[*common,'test/incremental_cache/model.test.cpp','-o',OUT/'model'])
 with tempfile.TemporaryDirectory(prefix='csp-incremental-') as tmp:
  temporary=Path(tmp)
  run('model',[OUT/'model',temporary/'sidecar'])
  # Use original SDK source/config; only RTOS/disk providers are synthetic.
  (temporary/'freertos').mkdir();(temporary/'freertos/FreeRTOS.h').write_text('#pragma once\n#define portTICK_PERIOD_MS 1\n')
  (temporary/'freertos/semphr.h').write_text('#pragma once\n')
  fatflags=['-I'+str(temporary),'-I'+str(CONFIG),'-I'+str(FAT)]
  for name in ['ff.c','ffunicode.c']:
   run('pinned-'+name+'-build',[cc,'-std=c11',*flags,'-Wno-unused-but-set-variable',*fatflags,'-c',FAT/name,'-o',temporary/(name+'.o')])
  run('fatfs-build',[*common,*fatflags,'test/incremental_cache/fatfs.test.cpp',temporary/'ff.c.o',temporary/'ffunicode.c.o','-o',OUT/'fatfs'])
  run('fatfs',[OUT/'fatfs'])
  workerflags=['-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_OWNER_CLOUD_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_REMOTE_HEALTH=1','-DCSP_ENABLE_SCAN_IDLE_GATE=1','-DCSP_ENABLE_FILE_INCREMENTAL_CACHE=1','-Itest/scan_idle_gate/stubs']
  run('worker-build',[cxx,'-std=c++17',*flags,*workerflags,'-Iinclude','test/incremental_cache/worker.test.cpp','src/pro_core.cpp','src/file_incremental_cache.cpp','src/remote_health_worker.cpp','src/remote_health_model.cpp','-Wl,--wrap=opendir','-Wl,--wrap=readdir','-Wl,--wrap=closedir','-Wl,--wrap=stat','-Wl,--wrap=fopen','-Wl,--wrap=fread','-Wl,--wrap=fwrite','-Wl,--wrap=fclose','-o',OUT/'worker'])
  for scenario in ['hundred-plus-two','new-file','same-size-preserved-time','overflow','receipt','corrupt','missing','mapping-fallback','interrupted','reboot','audit','scope','pending-not-reused','worker-restart','worker-audit','worker-audit-not-postponed']:
   run('worker-'+scenario,[OUT/'worker',scenario,temporary/('cache-'+scenario)])
 stable=before==hashes();assert stable
 sdk={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [FAT/'ff.c',FAT/'ff.h',FAT/'ffconf.h',FAT/'ffunicode.c',CONFIG/'sdkconfig.h']}
 report=dict(schema='cloud-stick-incremental-cache-source-tests/v1',passed=True,source_stable=stable,source_sha256=before,pinned_fatfs_sha256=sdk,records=records,desktop_only=True,device_contact=False,cloud_contact=False,physical_sd_contact=False,sanitizers=['ASan','UBSan','LeakSanitizer'],not_tested=['target build','target heap/stack/timing','physical SD/MSC writes','real TLS/cloud','power interruption on target'])
 (OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS bounded incremental cache source tests',flush=True)
if __name__=='__main__':main()
