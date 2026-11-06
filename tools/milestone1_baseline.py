"""Collect the immutable Milestone 1 baseline. Never accesses a device or secrets."""
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path('/config/.unmanic/research/cloud-stick-m1-20261103/baseline-source')
OUT = ROOT / 'artifacts/milestone1/baseline'
CORE = Path('/config/.unmanic/research/cloud-stick-platformio')
BUILD = SOURCE / '.pio/build/tdongle_s3'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((OUT / 'baseline_manifest.json').read_text())
    text = (OUT / 'build.log').read_text()
    assert '[SUCCESS]' in text
    manifest['build'] = {
        'success': True,
        'environment': 'tdongle_s3',
        'static_ram_bytes': int(re.findall(r'RAM:.*used (\d+) bytes', text)[-1]),
        'program_space_bytes': int(re.findall(r'Flash:.*used (\d+) bytes', text)[-1]),
        'firmware_bytes': (BUILD / 'firmware.bin').stat().st_size,
        'firmware_sha256': sha(BUILD / 'firmware.bin'),
        'partitions_source_sha256': sha(SOURCE / 'partitions.csv'),
        'platformio_ini_sha256': sha(SOURCE / 'platformio.ini'),
    }
    for name in ['firmware.bin', 'firmware.elf', 'firmware.map', 'partitions.bin', 'bootloader.bin']:
        shutil.copy2(BUILD / name, OUT / name)
    framework = CORE / 'packages/framework-arduinoespressif32'
    libs = CORE / 'packages/framework-arduinoespressif32-libs'
    sdk = libs / 'esp32s3/qio_qspi/include/sdkconfig.h'
    config = dict(re.findall(r'^#define (CONFIG_\w+) (.+)$', sdk.read_text(), re.M))
    flags = ['CONFIG_BT_ENABLED', 'CONFIG_BT_NIMBLE_ENABLED',
             'CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE', 'CONFIG_BT_NIMBLE_MAX_CONNECTIONS',
             'CONFIG_ESP_COEX_SW_COEXIST_ENABLE', 'CONFIG_ESP_INT_WDT_TIMEOUT_MS',
             'CONFIG_ESP_TASK_WDT_TIMEOUT_S', 'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_0',
             'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1',
             'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2',
             'CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_PATCH_VERSION',
             'CONFIG_NVS_ENCRYPTION', 'CONFIG_SECURE_FLASH_ENC_ENABLED', 'CONFIG_SECURE_BOOT']
    manifest['dependencies'] = {
        'platform': 'pioarduino55.03.312', 'platformio_core': '6.2.0',
        'arduino': json.loads((framework / 'package.json').read_text())['version'],
        'idf_libraries': json.loads((libs / 'package.json').read_text())['version'],
        'libraries': (SOURCE / '.pio/libdeps/tdongle_s3/integrity.dat').read_text().splitlines(),
        'sdkconfig_sha256': sha(sdk),
        'sdk_flags': {name: config.get(name, 'not defined') for name in flags},
    }
    memory = []
    with (BUILD / 'firmware.elf').open('rb') as stream:
        elf = ELFFile(stream)
        sections = {s.name: {'bytes': s['sh_size'], 'address': hex(s['sh_addr']), 'flags': s['sh_flags']}
                    for s in elf.iter_sections() if s['sh_flags'] & 2}
        for symbol in elf.get_section_by_name('.symtab').iter_symbols():
            section_index = symbol['st_shndx']
            if isinstance(section_index, int) and symbol['st_size']:
                section = elf.get_section(section_index)
                if section.name in ['.dram0.bss', '.dram0.data', '.noinit', '.rtc.data']:
                    memory.append({'bytes': symbol['st_size'], 'section': section.name, 'name': symbol.name})
    (OUT / 'elf_sections.json').write_text(json.dumps(sections, indent=2) + '\n')
    nm = CORE / 'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm'
    symbols = subprocess.check_output([str(nm), '-S', '--size-sort', '--radix=d', '-C',
                                       str(BUILD / 'firmware.elf')], text=True)
    (OUT / 'symbols_by_size.txt').write_text(symbols)
    manifest['largest_static_data'] = sorted(memory, key=lambda x: x['bytes'], reverse=True)[:30]
    paths = {'public_release': Path('/config/.unmanic/releases/cloud-stick-pro-0.3.3-dev-9d222a1.zip'),
             'history_bundle': Path('/config/.unmanic/releases/cloud-stick-pro-0.3.3-dev-9d222a1.bundle'),
             'preserved_partitions': ROOT / 'artifacts/0.3.3/partitions.bin'}
    manifest['recovery_artifacts'] = {name: {'exists': p.exists(), 'bytes': p.stat().st_size,
                                            'sha256': sha(p)} for name, p in paths.items()}
    manifest['historical_private_artifacts'] = {
        'configuration_snapshot': 'Previously retained NVS snapshot; not refreshed or decoded',
        'live_receipts': 'Not read. Fresh safe recovery backup remains a hardware-readiness gate',
        'original_flash': 'Previously retained original pre-Pro backup; not the current working app',
    }
    (OUT / 'baseline_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'build': manifest['build'], 'dependencies': manifest['dependencies'],
                      'device_query': manifest['device_query']}, indent=2))


if __name__ == '__main__':
    main()
