"""Verify pinned replacement ownership and Arduino reservation in a built ELF.

Read-only artifact inspection; never invokes upload, serial or a device command.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--env',default='tdongle_s3_ble_m1_isolated');parser.add_argument('--disabled',action='store_true')
args=parser.parse_args()
spec=importlib.util.spec_from_file_location('pinned_ble_sdk',ROOT/'tools/pinned_ble_sdk.py')
sdk=importlib.util.module_from_spec(spec);spec.loader.exec_module(sdk);sdk.verify()
build=ROOT/'.pio/build'/args.env
elf=build/'firmware.elf';mapping=(build/'firmware.map').read_text()
core=Path(os.environ.get('PLATFORMIO_CORE_DIR','/config/.unmanic/research/cloud-stick-platformio'))
tool=core/'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-'
nm=subprocess.run([str(tool)+'nm','-n',str(elf)],capture_output=True,text=True,check=True).stdout
symbols={m.group(3):(int(m.group(1),16),m.group(2)) for line in nm.splitlines() if (m:=re.fullmatch(r'([0-9a-fA-F]+)\s+(\w)\s+(\S+)',line))}
if not args.disabled:
    for member in ['protocomm_nimble.c.obj','nimble_port_freertos.c.obj','security2.c.obj','ble_store_nvs.c.obj']:
        assert '('+member+')' not in mapping,'Precompiled duplicate SDK selected: '+member
out=ROOT/'artifacts/milestone1-corrections/sdk-link'/args.env;out.mkdir(parents=True,exist_ok=True)
expected={'protocomm_ble_start':'ble_sdk_transport.c.o','protocomm_ble_stop':'ble_sdk_transport.c.o','protocomm_security2':'ble_sdk_security2.c.o','esp_nimble_enable':'ble_sdk_tasks.c.o','nimble_port_freertos_deinit':'ble_sdk_tasks.c.o','ble_store_config_conf_init':'ble_sdk_volatile_store.c.o'}
stop_repair=args.env in ('tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7','tdongle_s3_product_rc1','tdongle_s3_product_dashboard1','tdongle_s3_product_speed1','tdongle_s3_product_rc2','tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev','tdongle_s3_product_speed2')
stop_symbols={'npl_freertos_eventq_get':'ble_sdk_npl_stop.c.o','npl_freertos_eventq_put':'ble_sdk_npl_stop.c.o','npl_freertos_eventq_remove':'ble_sdk_npl_stop.c.o','npl_freertos_event_is_queued':'ble_sdk_npl_stop.c.o','npl_freertos_mempool_init':'ble_sdk_npl_stop.c.o','npl_freertos_mempool_deinit':'ble_sdk_npl_stop.c.o','ble_hs_init':'ble_sdk_host_stop.c.o','ble_hs_deinit':'ble_sdk_host_stop.c.o','ble_hs_timer_resched':'ble_sdk_host_stop.c.o'}
if stop_repair:
    for member in ('npl_os_freertos.c.obj','ble_hs.c.obj'):
        assert '('+member+')' not in mapping,'Precompiled NimBLE duplicate selected: '+member
    expected.update(stop_symbols)
if args.disabled:
    for name in set(expected)|set(stop_symbols):
        assert name not in symbols,'BLE symbol in disabled firmware: '+name
    assert '_setBleLibraryInUse' not in symbols,'BLE reservation constructor in disabled firmware'
    constructor_present=False
else:
    for name,filename in expected.items():
        assert name in symbols and symbols[name][1].isupper(),'Missing strong replacement: '+name
        assert re.search(r'^'+re.escape(name)+r'\s+[^\n]*src/'+re.escape(filename)+r'\s*$',mapping,re.M),'Wrong symbol provider: '+name
    # ESP-IDF combines init_array into .flash.rodata. Parse the ELF's section
    # table and read the actual initializer words instead of matching map text.
    data=elf.read_bytes();assert data[:6]==b'\x7fELF\x01\x01','Expected little-endian ELF32'
    shoff=struct.unpack_from('<I',data,32)[0];entsize,count=struct.unpack_from('<HH',data,46)
    sections=[struct.unpack_from('<10I',data,shoff+i*entsize) for i in range(count)]
    start=symbols['__init_array_start'][0];end=symbols['__init_array_end'][0]
    section=next(s for s in sections if s[3]<=start and end<=s[3]+s[5])
    initializers=struct.unpack_from('<'+str((end-start)//4)+'I',data,section[4]+start-section[3])
    constructor=symbols['_setBleLibraryInUse'][0]
    assert constructor in initializers,'BLE reservation constructor omitted from init array'
    constructor_present=True
    for name in ['_setBleLibraryInUse','protocomm_ble_stop','sec2_encrypt','sec2_decrypt','csp_ble_sdk_deinit_port']:
        disasm=subprocess.run([str(tool)+'objdump','-d','--disassemble='+name,str(elf)],capture_output=True,text=True,check=True).stdout
        (out/(name+'.disassembly.txt')).write_text(disasm)
result={'passed':True,'environment':args.env,'enabled':not args.disabled,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'map_sha256':hashlib.sha256(mapping.encode()).hexdigest(),'old_transport_task_security_store_archive_members_absent':None if args.disabled else True,'arduino_ble_reservation_in_init_array':constructor_present,'strong_symbol_providers':{} if args.disabled else expected,'disabled_elf_has_no_ble_runtime_symbols':True if args.disabled else None,'scope':'Built ELF/map ownership and initialization inspection only; no physical device or runtime claim'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
