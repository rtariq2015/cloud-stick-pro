"""Preserved lab6 rejection versus corrected actual adapter; no hardware."""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/route-startup-restart';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr');CLANG=DEPS/'lib/llvm-18/bin/clang++'
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
original=subprocess.check_output(['git','show','c1ab0f3eb4e5fd777988cead72a384b307b20120:src/owned_guest_route.cpp'],cwd=ROOT)
(OUT/'original-lab6.cpp').write_bytes(original)
sources={'old':OUT/'original-lab6.cpp','fixed':ROOT/'src/owned_guest_route.cpp'};records=[]
paths=['src/owned_guest_route.cpp','include/owned_guest_route.h','test/owned_guest_route/stubs/platform.h','test/owned_guest_route/configuration_restart.test.cpp','test/owned_guest_route/configuration_restart_tests.py']
before={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
def run(name,command,expected=None):
 p=subprocess.run(list(map(str,command)),cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=90)
 text=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(text)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',text))
 passed=(p.returncode!=0 and expected in text and not diagnostic) if expected else p.returncode==0 and not p.stderr and not diagnostic
 records.append({'name':name,'passed':passed,'exit':p.returncode,'expected_old_rejection':bool(expected),'sanitizer_diagnostic':diagnostic,'log_sha256':hashlib.sha256(text.encode()).hexdigest()})
 print(name,'PASS' if passed else 'FAIL',flush=True)
 if not passed:raise RuntimeError(text)
for variant,source in sources.items():
 folder=OUT/variant;folder.mkdir(exist_ok=True);(folder/'route_under_test.hpp').write_text('#include '+json.dumps(str(source))+'\n')
 binary=folder/'test'
 flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-DCSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','-DCSP_ENABLE_BACKUP_SETUP=1','-DCSP_USB_BENCH_AUTOMATION=1','-DCSP_PRODUCT_LAB6_PROFILE=1','-I'+str(folder),'-Itest/owned_guest_route/stubs','-Iinclude']
 run(variant+'-build',[CLANG,*flags,*(['-DFIXED_ROUTE_EPOCH=1'] if variant=='fixed' else []),'test/owned_guest_route/configuration_restart.test.cpp','-o',binary])
 run(variant+'-startup-restart',[binary],expected='status().phase==Phase::Active' if variant=='old' else None)
assert before=={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}
(OUT/'results.json').write_text(json.dumps({'passed':True,'desktop_only':True,'device_contact':False,'source_stable':True,'source_sha256':before,'old_source_sha256':hashlib.sha256(original).hexdigest(),'fixed_source_sha256':hashlib.sha256(sources['fixed'].read_bytes()).hexdigest(),'records':records},indent=2)+'\n')
