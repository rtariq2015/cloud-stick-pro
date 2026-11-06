"""Source-stage public Wi-Fi policy/wire gates. No device/network operation."""
from pathlib import Path
import json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/public-wifi-assistance';OUT.mkdir(parents=True,exist_ok=True)
DEPS=Path('/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr')
CLANG=DEPS/'lib/llvm-18/bin/clang++'
JSON=Path('/config/.unmanic/research/cloud-stick-ble-auth-cleanup/.pio/libdeps/tdongle_s3_wifi_m2/ArduinoJson/src')
ENV=dict(os.environ,LD_LIBRARY_PATH=str(DEPS/'lib/x86_64-linux-gnu'),ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
       '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
       '-Iinclude','-Ilib/wifi_setup_model','-isystem',str(JSON)]
enabled=['-DCSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','-DCSP_ENABLE_OWNED_GUEST_ROUTE=1','-DCSP_ENABLE_BACKUP_SETUP=1']
commands=[('policy-build',[str(CLANG),*flags,*enabled,'src/public_wifi_assistance.cpp','test/public_wifi_assistance/policy.test.cpp','-o',str(OUT/'policy-tests')]),
          ('policy',[str(OUT/'policy-tests')]),
          ('clock-policy-build',[str(CLANG),*flags,*enabled,'src/public_wifi_assistance.cpp','test/public_wifi_assistance/clock_policy.test.cpp','-o',str(OUT/'clock-policy-tests')]),
          ('clock-policy',[str(OUT/'clock-policy-tests')])]
for mode in ['default','disabled','enabled']:
    defines=[] if mode=='default' else (enabled if mode=='enabled' else ['-DCSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=0'])
    binary=OUT/('wire-'+mode)
    commands.append(('wire-'+mode+'-build',[str(CLANG),*flags,*defines,'lib/wifi_setup_model/wifi_wire.cpp',
        'lib/wifi_setup_model/wifi_setup_model.cpp','test/public_wifi_assistance/wire.test.cpp','-o',str(binary)]))
    commands.append(('wire-'+mode,[str(binary)]))
sys.path.insert(0,str(ROOT/'tools'))
from create_wifi_owner_card import create_record,SYNTHETIC_PASSWORD
fixture=OUT/'synthetic-owner.bin';fixture.write_bytes(create_record('001122334455',SYNTHETIC_PASSWORD))
runtime=OUT/'runtime-tests'
fixture_source=(ROOT/'test/backup_integration/runtime_handoff.test.cpp').read_text()
assert fixture_source.count('\nint main(')==1
(OUT/'runtime_fixture_prefix.h').write_text(fixture_source.split('\nint main(',1)[0]+'\n')
runtime_flags=['-Itest/wifi_integration/stubs','-Itest/backup_integration','-I'+str(OUT),'-include','test/backup_integration/runtime_stubs.h','-pthread',
  '-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_ENABLE_WIFI_SETUP=1','-DCSP_USB_BENCH_AUTOMATION=1',
  '-DCONFIG_BT_NIMBLE_ENABLED=1','-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2=1',
  '-DCONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION=1']
commands.append(('runtime-build',[str(CLANG),*flags,*enabled,*runtime_flags,
  'test/public_wifi_assistance/runtime.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
  'src/public_wifi_assistance.cpp','lib/wifi_setup_model/wifi_setup_model.cpp',
  'lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp',
  'lib/wifi_setup_model/owner_record.cpp','-o',str(runtime)]))
for scenario in ['normal','cancel','not-selected','prepare-expiry','terminal','route-start-fail',
                 'route-cleanup-fail','probe-fail','probe-timeout','memory-low']:
    commands.append(('runtime-'+scenario,[str(runtime),str(fixture),scenario]))
clock_runtime=OUT/'advancing-clock-runtime-tests'
commands.append(('advancing-clock-runtime-build',[str(CLANG),*flags,*enabled,
  '-include','test/public_wifi_assistance/clock_runtime_stubs.h',*runtime_flags,
  'test/public_wifi_assistance/runtime.test.cpp','src/wifi_setup_runtime.cpp','src/wifi_setup_nvs.cpp',
  'src/public_wifi_assistance.cpp','lib/wifi_setup_model/wifi_setup_model.cpp',
  'lib/wifi_setup_model/wifi_profile_store.cpp','lib/wifi_setup_model/wifi_wire.cpp',
  'lib/wifi_setup_model/owner_record.cpp','-o',str(clock_runtime)]))
for scenario in ['normal','cancel','not-selected','prepare-expiry','terminal','route-start-fail',
                 'route-cleanup-fail','probe-fail','probe-timeout','memory-low']:
    commands.append(('advancing-clock-runtime-'+scenario,[str(clock_runtime),str(fixture),scenario]))
probe=OUT/'probe-task-tests'
commands.append(('probe-task-build',[str(CLANG),'-Itest/public_wifi_assistance/probe_stubs',*flags,*enabled,'-pthread',
    'src/public_wifi_probe.cpp','test/public_wifi_assistance/probe.test.cpp','-o',str(probe)]))
commands.append(('probe-task',[str(probe)]))
records=[]
for name,command in commands:
    result=subprocess.run(command,cwd=ROOT,env=ENV,capture_output=True,text=True,timeout=180)
    output=result.stdout+result.stderr;(OUT/(name+'.log')).write_text(output)
    diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:',output))
    passed=result.returncode==0 and not result.stderr and not diagnostic
    records.append({'name':name,'exit':result.returncode,'command':command,'sanitizer_diagnostic':diagnostic,'passed':passed})
    print(name,'PASS' if passed else 'FAIL',flush=True)
    (OUT/'results.json').write_text(json.dumps({'passed':all(r['passed'] for r in records),'desktop_only':True,'records':records},indent=2)+'\n')
    if not passed:print(output);raise SystemExit(1)
