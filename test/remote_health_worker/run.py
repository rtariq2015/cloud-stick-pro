from pathlib import Path
import os,subprocess,json,re,hashlib
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/remote-health-worker';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
ENV=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'))
records=[]
inputs=['src/sync_engine.cpp','src/remote_health_worker.cpp','src/remote_health_model.cpp',
        'include/remote_health_worker.h','include/remote_health_model.h','include/remote_health_publisher.h',
        'include/backup_worker.h','include/pro_device.h','include/ble_feature.h','include/usb_storage.h','src/usb_storage.cpp',
        'test/backup_integration/stubs/fakes.h','test/backup_integration/stubs/esp_random.h',
        'test/remote_health_worker/worker.test.cpp','test/remote_health_worker/observations.test.cpp',
        'test/remote_health_worker/run.py']
hashes={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs}
def run(name,args):
 p=subprocess.run(args,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180);text=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(text)
 passed=p.returncode==0 and not p.stderr and not re.search(r'AddressSanitizer|LeakSanitizer|runtime error:|UndefinedBehaviorSanitizer',text)
 records.append(dict(name=name,command=args,passed=passed,exit=p.returncode))
 (OUT/'results.json').write_text(json.dumps(dict(passed=all(r['passed'] for r in records),desktop_only=True,hardware_or_cloud=False,
   sanitizers=['ASan','UBSan','LeakSanitizer'],source_sha256=hashes,records=records),indent=2)+'\n')
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:print(text[-5000:]);raise SystemExit(1)
binary=OUT/'worker-health'
run('build',[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-omit-frame-pointer','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_REMOTE_HEALTH=1','-Itest/backup_integration/stubs','-Iinclude','src/sync_engine.cpp','src/remote_health_worker.cpp','src/remote_health_model.cpp','test/remote_health_worker/worker.test.cpp','-o',str(binary)])
for scenario in ['normal','snapshot','failure','retire-health']:run(scenario,[str(binary),scenario])
observation=OUT/'worker-observations'
run('observation-build',[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-omit-frame-pointer','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_REMOTE_HEALTH=1','-Itest/backup_integration/stubs','-Iinclude','src/remote_health_worker.cpp','src/remote_health_model.cpp','test/remote_health_worker/observations.test.cpp','-Wl,--wrap=time','-o',str(observation)])
run('observations',[str(observation)])
storage=(ROOT/'src/usb_storage.cpp').read_text()
getter=re.search(r'bool storage_read_view_active\(\)\{.*?\n\}',storage,re.S)
assert getter,'Actual health admission getter was not found'
accessor=OUT/'actual-storage-accessor.cpp'
accessor.write_text('''#include <cassert>
#include <cstdio>
constexpr int pdTRUE=1;
int token=0;int* mutex=nullptr;bool readMounted=false,driveRegistered=false;
int takeResult=1,takes=0,gives=0;
int xSemaphoreTake(int* value,unsigned ticks){assert(value==&token&&ticks==0);++takes;return takeResult;}
void xSemaphoreGive(int* value){assert(value==&token);++gives;}
'''+getter.group()+'''
int main(){
assert(storage_read_view_active()&&takes==0&&gives==0);
mutex=&token;takeResult=0;assert(storage_read_view_active()&&takes==1&&gives==0);
takeResult=1;assert(!storage_read_view_active()&&gives==1);
readMounted=true;assert(storage_read_view_active());readMounted=false;
driveRegistered=true;assert(storage_read_view_active());driveRegistered=false;
assert(!storage_read_view_active());
std::puts("PASS actual nonblocking accessor: unknown/busy/registered view deny health");
}
''')
run('accessor-build',[str(DEPS/'lib/llvm-18/bin/clang++'),'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(accessor),'-o',str(OUT/'actual-storage-accessor')])
run('actual-storage-accessor',[str(OUT/'actual-storage-accessor')])
assert all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest for name,digest in hashes.items())
final=json.loads((OUT/'results.json').read_text());final['sources_unchanged']=True
(OUT/'results.json').write_text(json.dumps(final,indent=2)+'\n')
