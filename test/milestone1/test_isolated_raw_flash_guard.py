"""Strict desktop verification of isolated raw public mutation denial wrappers."""
import json
import os
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2];TEST=ROOT/'test/milestone1'
OUT=ROOT/'artifacts/milestone1-corrections/raw-flash-guard';OUT.mkdir(parents=True,exist_ok=True)
flags=['-std=c++17','-g','-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-DCSP_ENABLE_BLE_MANAGEMENT=1','-DCSP_BLE_SYNTHETIC_ONLY=1','-DCSP_BLE_ISOLATED_TEST=1','-I',str(TEST/'sdk_lifecycle_stubs'),'-I',str(ROOT/'include')]
commands=[['clang++',*flags,'-c',str(ROOT/'src/ble_isolated_flash_guard.cpp'),'-o',str(OUT/'raw-guard.o')],['clang++',*flags,str(TEST/'isolated_raw_flash_guard.cpp'),str(OUT/'raw-guard.o'),'-o',str(OUT/'raw-guard')]]
for command in commands:subprocess.run(command,check=True)
undefined=subprocess.run(['nm','-u',str(OUT/'raw-guard.o')],text=True,capture_output=True,check=True).stdout
assert not re.search(r'\b(?:__real_|nvs_|esp_partition_|esp_flash_|spi_flash_)',undefined),undefined
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
run=subprocess.run([str(OUT/'raw-guard')],env=env,text=True,capture_output=True,timeout=10)
assert run.returncode==0 and not run.stderr,(run.returncode,run.stdout,run.stderr)
result={'passed':True,'denied_raw_mutation_entrypoints':4,'no_real_mutation_dependencies':True,'strict_sanitizers':True,'commands':commands,'scope':'Actual isolated wrappers only; not a proof about arbitrary SDK same-object/ROM/chip-driver paths or persistent device state','physical_device_test':False}
(OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n');(OUT/'test.log').write_text(run.stdout)
print(run.stdout,end='');print(json.dumps(result))
