"""Offline ELF/artifact comparison. Never opens hardware or private backups."""
import hashlib,json,re,shutil,subprocess
from pathlib import Path
from milestone1_compare import inspect

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/milestone1-corrections'
BASE=ROOT/'artifacts/milestone1'
CORE=Path('/config/.unmanic/research/cloud-stick-platformio')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    reports={};sections={}
    nm=CORE/'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm'
    for label,env in [('disabled','tdongle_s3'),('isolated','tdongle_s3_ble_m1_isolated')]:
        source=ROOT/'.pio/build'/env;target=OUT/label;target.mkdir(exist_ok=True)
        log=(OUT/(label+'-build.log')).read_text();assert '[SUCCESS]' in log
        sections[label],symbols=inspect(source)
        for name in ['firmware.bin','firmware.elf','firmware.map','partitions.bin']:
            shutil.copy2(source/name,target/name)
        syms=subprocess.check_output([str(nm),'-S','-C',str(source/'firmware.elf')],text=True)
        (target/'symbols.txt').write_text(syms)
        report={'environment':env,'firmware_bytes':(source/'firmware.bin').stat().st_size,
            'firmware_sha256':sha(source/'firmware.bin'),
            'program_space_bytes':int(re.findall(r'Flash:.*used (\d+) bytes',log)[-1]),
            'static_ram_bytes':int(re.findall(r'RAM:.*used (\d+) bytes',log)[-1]),
            'partition_sha256':sha(source/'partitions.bin'),
            'physical_runtime_measured':False,'security2_linked':'protocomm_security2' in syms}
        if label=='isolated':
            forbidden=['load_config(', 'save_config(', 'reset_config(', 'start_backup(',
                'WebDAVClient::','request_connection_check(', 'setup_identity(',
                'WiFiSTAClass::begin(', 'WiFiGenericClass::mode(', 'esp_wifi_init',
                'esp_wifi_connect','fs::LittleFSFS::begin(', 'diagnostics.cpp.o']
            report['absent_production_entrypoints']={name:name not in syms for name in forbidden}
            assert all(report['absent_production_entrypoints'].values()),report
            for name in ['__wrap_esp_partition_write','__wrap_esp_partition_write_raw','__wrap_esp_partition_erase_range']:
                assert re.search(r'\bT '+name+r'$',syms,re.M),name
            for name in ['esp_partition_write','esp_partition_write_raw','esp_partition_erase_range']:
                assert not re.search(r'\bT '+name+r'$',syms,re.M),'Unblocked original '+name
            for name in ['esp_flash_write','esp_flash_write_encrypted','esp_flash_erase_region','esp_flash_erase_chip']:
                assert not re.search(r'\bT '+name+r'$',syms,re.M),'Unblocked raw flash original '+name
            report['flash_mutation_entrypoints_replaced']=True
        assert report['security2_linked']==(label=='isolated')
        reports[label]=report
        (target/'build.json').write_text(json.dumps(report,indent=2)+'\n')
        (target/'sections.json').write_text(json.dumps(sections[label],indent=2)+'\n')
    expected=sha(ROOT/'artifacts/0.3.3/partitions.bin')
    assert all(r['partition_sha256']==expected for r in reports.values())
    oldsec,_=inspect(BASE/'disabled')
    default_comparison={name:{'before':oldsec.get(name),'after':sections['disabled'].get(name),
        'equal':oldsec.get(name)==sections['disabled'].get(name)} for name in set(oldsec)|set(sections['disabled'])}
    assert all(v['equal'] for k,v in default_comparison.items() if k!='.flash.rodata')
    old=json.loads((BASE/'BUILD_COMPARISON.json').read_text())['builds']
    result={'builds':reports,'default_allocated_sections_vs_d541c13':default_comparison,
        'change_vs_d541c13':{label:{key:reports[label][key]-old[prior][key]
            for key in ['firmware_bytes','static_ram_bytes','program_space_bytes']}
            for label,prior in [('disabled','disabled'),('isolated','enabled')]},
        'partition_binaries_unchanged':True,
        'comparison_limit':'Isolation removes production services; size delta is not BLE runtime cost. ELF/static data cannot measure heap or task stacks.'}
    (OUT/'BUILD_COMPARISON.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='default_allocated_sections_vs_d541c13'},indent=2))
if __name__=='__main__':main()
