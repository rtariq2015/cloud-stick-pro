"""Strict desktop execution of actual isolated setup/loop/runtime, no hardware."""
from pathlib import Path
import hashlib,json,os,subprocess
root=Path(__file__).resolve().parents[2]
test=root/'test/milestone1_corrections'
out=root/'artifacts/milestone1-corrections/isolation';out.mkdir(parents=True,exist_ok=True)
library=root/'.pio/libdeps/tdongle_s3_ble_m1_isolated/ArduinoJson'
if not library.exists():library=root/'.pio/libdeps/tdongle_s3/ArduinoJson'
assert json.loads((library/'library.json').read_text())['version']=='7.4.3'
binary=out/'isolated-runtime-tests'
cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-g','-O0','-fsanitize=address,undefined','-fno-omit-frame-pointer',
 '-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_BLE_SYNTHETIC_ONLY=1','-DCSP_BLE_ISOLATED_TEST=1',
 '-I'+str(test/'isolation_stubs'),'-I'+str(root/'include'),'-isystem',str(library/'src'),
 str(test/'isolated_boot_runtime.cpp'),'-o',str(binary)]
build=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(build.stdout+build.stderr);build.check_returncode()
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
records=[];logs=[]
for case in ['normal','failed-stop','memory-not-restored','prepare-failed']:
 result=subprocess.run([str(binary),case],capture_output=True,text=True,env=env,timeout=15)
 logs.append(result.stdout+result.stderr);records.append({'case':case,'exit_code':result.returncode,'stderr_empty':not result.stderr})
 if result.returncode or result.stderr:
  (out/'tests.log').write_text(''.join(logs));raise SystemExit('FAIL isolated runtime '+case+': '+result.stderr)
(out/'tests.log').write_text(''.join(logs))
sources=['src/main.cpp','src/ble_isolated_runtime.cpp','src/ble_isolated_flash_guard.cpp','platformio.ini','include/ble_feature.h','test/milestone1_corrections/isolated_boot_runtime.cpp']
(out/'results.json').write_text(json.dumps({'pass':True,'cases':records,'build_command':cmd,'compiler_stderr':build.stderr,
 'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in sources},
 'checks':['configured sentinel not loaded','zero network/cloud/backup/receipt/persistent-diagnostic calls',
 'USB initialized/polled','STATUS/LOG volatile and mutation commands denied',
 'seven actual partition/raw flash wrappers deny with no real mutation','host stack watermark and raw flash denial telemetry forwarded','3-second physical admission',
 '10-second hold cannot reset','window expiry/release/rehold','uncertain stop stays blocked without retry'],
 'scope':'actual isolated setup/loop/runtime, actual lifecycle/parser/ArduinoJson7.4.3; USB/radio/clock/heap/system primitives stubbed; no physical-device evidence',
 'strict_sanitizers':'ASan+UBSan halt_on_error, leak detection, fail on any runtime stderr'},indent=2)+'\n')
print('PASS 4 actual isolated main/runtime cases; boot preparation/failure and memory telemetry verified; strict ASan/UBSan and empty runtime stderr')
