"""Record a source-only link budget. Never open a device or install an image."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/backup-wifi-integration'
CORE = Path(os.environ.get('PLATFORMIO_CORE_DIR',
                          '/config/.unmanic/research/cloud-stick-platformio'))
BIN = CORE / 'packages/toolchain-xtensa-esp-elf/bin'


def command(*args):
    return subprocess.run(list(map(str, args)), cwd=ROOT, text=True,
                          capture_output=True, check=True).stdout


def digest(path):
    data = path.read_bytes()
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def inspect(environment):
    directory = ROOT / '.pio/build' / environment
    elf = directory / 'firmware.elf'
    symbols = command(BIN / 'xtensa-esp32s3-elf-nm', '-C', elf)
    sizes = command(BIN / 'xtensa-esp32s3-elf-size', '-A', elf)
    sections = {}
    for line in sizes.splitlines():
        match = re.fullmatch(r'(\.[\w.]+)\s+(\d+)\s+(\d+)', line.strip())
        if match:
            sections[match[1]] = int(match[2])
    return {
        'environment': environment,
        'files': {name: digest(directory / name) for name in
                  ['firmware.bin', 'firmware.elf', 'firmware.map', 'partitions.bin']},
        'elf_sections': sections,
        'static_ram_bytes': sum(sections.get(name, 0) for name in
                                ['.dram0.data', '.dram0.bss', '.noinit']),
    }, symbols


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    probe, symbols = inspect('tdongle_s3_backup_link_probe')
    disabled, disabled_symbols = inspect('tdongle_s3')
    required = ['start_backup(CloudConfig const&)', 'WebDAVClient::put(',
                'WebDAVClient::verify(', 'pro::FileJournal::load(',
                'pro::FileJournal::save(', 'wifi_setup_runtime_setup()']
    for symbol in required:
        assert symbol in symbols, f'Probe lost actual implementation: {symbol}'
    assert 'load_config(' not in symbols, 'Probe linked saved cloud configuration loader'
    assert 'wifi_setup_runtime_setup()' not in disabled_symbols
    assert 'protocomm_ble_start' not in disabled_symbols
    # A linker root retains the worker solely to measure its cost. Detect a
    # direct call accidentally added to the setup runtime. This is a static
    # check, not a complete indirect-call proof or a physical containment test.
    assembly = command(BIN / 'xtensa-esp32s3-elf-objdump', '-d',
                       ROOT / '.pio/build/tdongle_s3_backup_link_probe/firmware.elf')
    calls = [line for line in assembly.splitlines()
             if re.search(r'\bcall(?:0|4|8|12)\b', line)
             and '<_Z12start_backupRK11CloudConfig>' in line]
    assert not calls, 'Size-only probe unexpectedly calls start_backup'
    baseline = json.loads((ROOT / 'artifacts/wifi-radio-release3/build-manifest.json').read_text())
    partition = baseline['partitions.bin']['sha256']
    assert probe['files']['partitions.bin']['sha256'] == partition
    assert disabled['files']['partitions.bin']['sha256'] == partition
    tests = json.loads((OUT / 'source-test-results.json').read_text())
    assert len(tests['tests']) == 2 and all(item['passed'] for item in tests['tests'])
    source_files = ['include/ble_feature.h', 'include/pro_device.h', 'platformio.ini',
                    'src/wifi_setup_runtime.cpp',
                    'test/wifi_integration/bench_feature_flags.test.py',
                    'tools/check_backup_link_probe.py']
    result = {
        'scope': 'Source-only link budget; no functioning backup/setup bridge',
        'hardware_candidate': False, 'device_contact': False,
        'vault_access': False, 'microSD_access': False, 'real_cloud_access': False,
        'branch': command('git', 'branch', '--show-current').strip(),
        'base_commit': command('git', 'rev-parse', 'HEAD').strip(),
        'source_files': {name: digest(ROOT / name) for name in source_files},
        'probe': probe, 'disabled': disabled,
        'probe_static_ram_delta_from_installed_radio3': probe['static_ram_bytes'] - baseline['static_ram'],
        'worker_implementations_retained': required,
        'saved_cloud_loader_absent': True,
        'direct_calls_to_start_backup': calls,
        'partition_matches_preserved_layout': True,
        'runtime_quiescence_or_heap_reclamation_tested': False,
        'checks': tests,
    }
    (OUT / 'link-budget.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({
        'passed': True, 'hardware_candidate': False,
        'probe_static_ram': probe['static_ram_bytes'],
        'probe_bin': probe['files']['firmware.bin'],
        'disabled_static_ram': disabled['static_ram_bytes'],
        'partition_unchanged': True,
    }, indent=2))


if __name__ == '__main__':
    main()
