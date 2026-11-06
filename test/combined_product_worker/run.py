#!/usr/bin/env python3
"""Focused merged-worker source gate. No board, network, SD or private data."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/combined-product-worker'
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),
         ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',
         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
RECORDS=[]
SOURCES=['src/sync_engine.cpp','src/remote_health_worker.cpp',
         'src/remote_health_model.cpp','src/file_history.cpp','src/pro_event_log.cpp',
         'include/backup_worker.h','include/remote_health_worker.h',
         'include/remote_health_model.h','include/remote_health_publisher.h',
         'include/file_history.h','include/pro_event_log.h','include/ble_feature.h',
         'test/event_log/io.h']

def hashes():
    files=[ROOT/p for p in SOURCES]+list((ROOT/'test/combined_product_worker').rglob('*'))
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(files) if p.is_file()}

def run(name,cmd):
    result=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
    output=result.stdout+result.stderr
    log=OUT/(name+'.log');log.write_text(output)
    diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    passed=result.returncode==0 and not result.stderr and not diagnostic
    RECORDS.append(dict(name=name,command=list(map(str,cmd)),exit=result.returncode,
                        stderr_empty=not result.stderr,sanitizer_diagnostic=diagnostic,
                        passed=passed,log_sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:raise RuntimeError(output)

def main():
    OUT.mkdir(parents=True,exist_ok=True);before=hashes()
    flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
           '-Wno-misleading-indentation','-fno-omit-frame-pointer','-pthread',
           '-fsanitize=address,undefined','-fno-sanitize-recover=all',
           '-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1',
           '-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_REMOTE_HEALTH=1',
           '-DCSP_ENABLE_FILE_HISTORY=1','-DCSP_FILE_HISTORY_TESTING=1',
           '-DCSP_ENABLE_EVENT_LOG=1','-Itest/combined_product_worker/stubs','-Iinclude',
           '-include','test/combined_product_worker/history_alloc.h',
           '-include','test/event_log/io.h']
    with tempfile.TemporaryDirectory(prefix='csp-combined-worker-') as tmp:
        binary=OUT/'combined-worker'
        run('build',[DEPS/'lib/llvm-18/bin/clang++',*flags,
                     '-DCSP_EVENT_LOG_ROOT="'+tmp+'/events"',
                     'src/sync_engine.cpp','src/remote_health_worker.cpp',
                     'src/remote_health_model.cpp','src/file_history.cpp','src/pro_event_log.cpp',
                     'test/combined_product_worker/worker.test.cpp',
                     '-Wl,--wrap=opendir','-o',binary])
        for scenario in ['normal','held-view','retire-health','maintenance-health','cloud-tls','health-failure']:
            # Event snapshots are private synthetic fixtures, reset between
            # processes so startup/retirement actually exercise both writes.
            import shutil
            shutil.rmtree(Path(tmp)/'events',ignore_errors=True)
            run(scenario,[binary,scenario])
    after=hashes();assert before==after,'Linked source/test changed during gate'
    result=dict(passed=True,desktop_only=True,source_stable=True,strict_asan_ubsan=True,
                leak_detection=True,source_sha256=after,records=RECORDS,
                actual_components=['merged sync_engine','health observer/model','history cache','event log'],
                mocked_components=['RTOS','SD read view','receipt enumeration','DAV/TLS transport'],
                not_tested=['physical USB/BLE','real cloud','target stack/heap',
                            'actual portal-probe runtime (separate existing gate)'],
                device_contact=False,cloud_contact=False,real_sd_operations=False)
    (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS focused combined native worker integration',flush=True)

if __name__=='__main__':main()
