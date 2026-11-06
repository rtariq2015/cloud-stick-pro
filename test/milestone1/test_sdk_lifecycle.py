"""Strict fault injection of the real lifecycle adapter and pinned C bodies.

No radio, target/RTOS scheduling or device heap behavior is emulated/proved.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
TEST=ROOT/'test/milestone1'
OUT=ROOT/'artifacts/milestone1-corrections/sdk-lifecycle'
OUT.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('pinned_ble_sdk',ROOT/'tools/pinned_ble_sdk.py')
sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk);sdk.verify()

def extract(source,name):
    match=re.search(r'(?:static\s+)?(?:esp_err_t|void|int)\s+'+name+r'\s*\([^;]*?\)\s*\{',source)
    if not match:raise AssertionError('Missing pinned SDK function '+name)
    depth=1;end=match.end()
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]+'\n'

transport=(ROOT/'sdk/pinned_ble/patched/protocomm_nimble.c').read_text()
task=(ROOT/'sdk/pinned_ble/patched/nimble_port_freertos.c').read_text()
store=(ROOT/'sdk/pinned_ble/upstream/ble_store_config.c').read_text()
(OUT/'ble_store_config_init.exact.inc').write_text(extract(store,'ble_store_config_init'))
security=(ROOT/'sdk/pinned_ble/patched/security2.c').read_text()
counter=extract(security,'sec2_gcm_iv_counter_increment')
assert security.count('if ((cur_session->iv[8] | cur_session->iv[9] |')==2
(OUT/'sec2_gcm_iv_counter_increment.patched.inc').write_text(counter)
for name in ['simple_ble_start','simple_ble_advertise','simple_ble_on_sync','protocomm_ble_stop','esp_nimble_enable']:
    (OUT/(name+'.patched.inc')).write_text(extract(task if name=='esp_nimble_enable' else transport,name))
assert 'if (!csp_ble_sdk_controller_idle() || !csp_ble_sdk_wait_host_exit()) return err;' in transport
assert 'if (csp_ble_sdk_enter_host_loop()) nimble_port_run();' in extract(transport,'nimble_host_task')
flags=['-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_BLE_SYNTHETIC_ONLY=1','-DCSP_BLE_ISOLATED_TEST=1']
for include in [TEST/'sdk_lifecycle_stubs',ROOT/'include',TEST,OUT]:flags+=['-I',str(include)]
arduino=Path(os.environ.get('PLATFORMIO_CORE_DIR','/config/.unmanic/research/cloud-stick-platformio'))/'packages/framework-arduinoespressif32/cores/esp32'
flags+=['-I',str(arduino)]
# clang's sanitizer runtime is used consistently for both C and C++ objects.
commands=[['clang','-std=gnu11',*flags,'-c',str(TEST/'sdk_lifecycle_faults.c'),'-o',str(OUT/'c.o')],
          ['clang++','-std=c++17',*flags,str(TEST/'sdk_lifecycle_faults.cpp'),str(ROOT/'src/ble_sdk_lifecycle.cpp'),str(OUT/'c.o'),'-o',str(OUT/'sdk-lifecycle-faults')],
          ['clang','-std=gnu11',*flags,'-c',str(ROOT/'src/ble_sdk_volatile_store.c'),'-o',str(OUT/'volatile-store.o')],
          ['clang','-std=gnu11',*flags,str(TEST/'sdk_volatile_store.c'),str(OUT/'volatile-store.o'),'-o',str(OUT/'sdk-volatile-store')],
          ['clang','-std=gnu11',*flags,str(TEST/'security2_counter.c'),'-o',str(OUT/'security2-counter')]]
for command in commands:subprocess.run(command,check=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
cases=['healthy','init-failure','partial-init-failure','gatt-failure','name-failure','task-oom','task-oom-deinit-failure','gatt-deinit-failure','sync-failure','running-sync-timeout','late-start','never-scheduled','entered-before-loop-timeout','stop-failure','deinit-failure','controller-still-enabled']
log=[]
undefined=subprocess.run(['nm','-u',str(OUT/'volatile-store.o')],text=True,capture_output=True,check=True).stdout
assert not re.search(r'\b(?:nvs_|esp_partition_|esp_flash_|spi_flash_)',undefined),undefined
run=subprocess.run([str(OUT/'sdk-volatile-store')],env=env,text=True,capture_output=True,timeout=10)
assert run.returncode==0 and not run.stderr,(run.returncode,run.stdout,run.stderr)
log.append(run.stdout)
run=subprocess.run([str(OUT/'security2-counter')],env=env,text=True,capture_output=True,timeout=10)
assert run.returncode==0 and not run.stderr,(run.returncode,run.stdout,run.stderr)
log.append(run.stdout)
for case in cases:
    run=subprocess.run([str(OUT/'sdk-lifecycle-faults'),case],env=env,text=True,capture_output=True,timeout=10)
    assert run.returncode==0,(case,run.returncode,run.stdout,run.stderr)
    assert not run.stderr,(case,run.stderr)
    log.append(run.stdout)
result={'passed':True,'cases':cases,'checks':len(cases),'volatile_store_callbacks_and_no_persistence_dependencies':True,'unaligned_counter_carry_wrap_checks':8,'commands':commands,'patched_source_sha256':hashlib.sha256(transport.encode()).hexdigest(),'scope':'Actual corrected application SDK lifecycle + verbatim extracted pinned C startup/advertise/sync/stop/task-allocation/RAM-store-init/counter functions; fake RTOS/controller effects','strict_sanitizers':True,'stderr_rejected_even_exit_zero':True,'physical_device_test':False}
(OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
(OUT/'faults.log').write_text(''.join(log))
print(''.join(log),end='')
print(json.dumps(result))
