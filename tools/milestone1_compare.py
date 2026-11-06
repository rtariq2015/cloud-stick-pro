"""Collect build artifacts without device, source or configuration changes."""
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/milestone1'
CORE=Path('/config/.unmanic/research/cloud-stick-platformio')
BASE=Path('/config/.unmanic/research/cloud-stick-m1-20261103/baseline-source')


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(build):
    with (build/'firmware.elf').open('rb') as f:
        elf=ELFFile(f)
        sections={s.name:{'bytes':s['sh_size'],'sha256':hashlib.sha256(s.data()).hexdigest()}
                  for s in elf.iter_sections() if s['sh_flags']&2}
        symbols=[]
        for symbol in elf.get_section_by_name('.symtab').iter_symbols():
            index=symbol['st_shndx']
            if isinstance(index,int) and symbol['st_size']:
                section=elf.get_section(index).name
                if section in ['.dram0.bss','.dram0.data','.noinit','.rtc.data']:
                    symbols.append({'name':symbol.name,'section':section,'bytes':symbol['st_size']})
    return sections,symbols


def main():
    reports={};all_sections={};all_symbols={}
    for label,env,log in [('disabled','tdongle_s3','disabled-build.log'),
                          ('enabled','tdongle_s3_ble_m1','ble-enabled-build.log')]:
        build=ROOT/'.pio/build'/env
        target=OUT/label;target.mkdir(exist_ok=True)
        log_text=(OUT/log).read_text()
        assert '[SUCCESS]' in log_text,(label,'build not successful')
        sections,symbols=inspect(build);all_sections[label]=sections;all_symbols[label]=symbols
        for name in ['firmware.bin','firmware.elf','firmware.map','partitions.bin']:
            shutil.copy2(build/name,target/name)
        nm=CORE/'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm'
        symbol_text=subprocess.check_output([str(nm),'-S','--size-sort','-C',str(build/'firmware.elf')],text=True)
        (target/'symbols_by_size.txt').write_text(symbol_text)
        report={'environment':env,'firmware_bytes':(build/'firmware.bin').stat().st_size,
                'firmware_sha256':sha(build/'firmware.bin'),
                'static_ram_bytes':int(re.findall(r'RAM:.*used (\d+) bytes',log_text)[-1]),
                'program_space_bytes':int(re.findall(r'Flash:.*used (\d+) bytes',log_text)[-1]),
                'partitions_binary_sha256':sha(build/'partitions.bin'),
                'security2_linked':'protocomm_security2' in symbol_text,
                'physical_runtime_heap_measured':False,
                'largest_static_data':sorted(symbols,key=lambda x:x['bytes'],reverse=True)[:30]}
        reports[label]=report
        (target/'build.json').write_text(json.dumps(report,indent=2)+'\n')
        (target/'elf_sections.json').write_text(json.dumps(sections,indent=2)+'\n')
    assert reports['disabled']['security2_linked'] is False
    assert reports['enabled']['security2_linked'] is True
    baseline=json.loads((OUT/'baseline/baseline_manifest.json').read_text())
    protected=['partitions.csv','boards/tdongles3.json','src/usb_storage.cpp','include/usb_storage.h',
               'src/pro_journal.cpp','include/pro_journal.h','src/pro_core.cpp','include/pro_core.h',
               'include/pro_snapshot.h','include/pro_blockio.h','src/config_loader.cpp','include/config_loader.h',
               'src/webdav_client.cpp','include/webdav_client.h','src/lcd.cpp','src/guest_wifi.cpp']
    protection={name:sha(ROOT/name)==sha(BASE/name) for name in protected}
    assert all(protection.values()),protection
    assert reports['disabled']['partitions_binary_sha256']==reports['enabled']['partitions_binary_sha256']==sha(OUT/'baseline/partitions.bin')
    old={(s['name'],s['section']):s['bytes'] for s in all_symbols['disabled']}
    ram_delta=[]
    for s in all_symbols['enabled']:
        before=old.get((s['name'],s['section']),0)
        if s['bytes']!=before:ram_delta.append({**s,'disabled_bytes':before,'delta':s['bytes']-before})
    baseline_sections,_=inspect(OUT/'baseline')
    default_sections={name:{'baseline_bytes':baseline_sections.get(name,{}).get('bytes'),
                            'disabled_bytes':all_sections['disabled'].get(name,{}).get('bytes'),
                            'content_equal':baseline_sections.get(name)==all_sections['disabled'].get(name)}
                      for name in sorted(set(baseline_sections)|set(all_sections['disabled']))}
    (OUT/'DEFAULT_BASELINE_SECTION_COMPARISON.json').write_text(json.dumps(default_sections,indent=2)+'\n')
    assert all(value['content_equal'] for name,value in default_sections.items() if name!='.flash.rodata')
    result={'builds':reports,'delta':{k:reports['enabled'][k]-reports['disabled'][k]
                                  for k in ['firmware_bytes','static_ram_bytes','program_space_bytes']},
            'baseline_static_ram_bytes':baseline['build']['static_ram_bytes'],
            'protected_sources_unchanged':protection,
            'partition_binaries_identical':True,
            'default_vs_immutable_baseline':'All allocated section sizes/content match except compile-time timestamp bytes in .flash.rodata',
            'largest_new_or_changed_static_data':sorted(ram_delta,key=lambda x:x['delta'],reverse=True)[:30],
            'runtime_memory_note':'Linker sizes only. BLE init/SRP/deinit and subsequent TLS heap require physical measurement; SD cannot replace internal heap.'}
    (OUT/'BUILD_COMPARISON.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'builds':{k:{x:y for x,y in v.items() if x!='largest_static_data'} for k,v in reports.items()},'delta':result['delta'],'protected_sources_unchanged':all(protection.values()),'partition_binaries_identical':True},indent=2))


if __name__=='__main__':main()
