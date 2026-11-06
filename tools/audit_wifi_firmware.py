"""Offline ELF/partition/stack audit of already-built review artifacts."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/wifi-integration'
TOOL = '/config/.unmanic/research/cloud-stick-platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-'
builds = json.loads((OUT / 'firmware-builds.json').read_text())
report = {}
for name, env in [('disabled', 'tdongle_s3'), ('m1', 'tdongle_s3_ble_m1_isolated'), ('m2', 'tdongle_s3_wifi_m2')]:
    elf = OUT / name / 'firmware.elf'
    symbols = subprocess.check_output([TOOL + 'nm', '-C', str(elf)], text=True)
    sections = subprocess.check_output([TOOL + 'size', '-A', str(elf)], text=True)
    (OUT / (name + '-symbols.txt')).write_text(symbols)
    (OUT / (name + '-sections.txt')).write_text(sections)
    for filename, expected in builds[name]['artifacts'].items():
        raw = (OUT / name / filename).read_bytes()
        assert len(raw) == expected['bytes'] and hashlib.sha256(raw).hexdigest() == expected['sha256']
    summary = builds[name]['summary']
    data = {'program_bytes': int(re.search(r'Flash:.*?used (\d+) bytes', summary).group(1)),
        'static_ram_bytes': int(re.search(r'RAM:.*?used (\d+) bytes', summary).group(1)),
        'partition_sha256': builds[name]['artifacts']['partitions.bin']['sha256']}
    if name == 'disabled':
        assert all(s not in symbols for s in ['wifi_setup_runtime', 'wifi_setup_endpoint',
            'csp_ble_sdk_start', 'pro::wifi_setup::'])
    if name == 'm2':
        for symbol in ['sync_engine', 'webdav', 'journal_', 'portal_', 'load_config',
                       'setup_identity', 'setup_network', 'synthetic_salt', 'synthetic_verifier']:
            assert symbol not in symbols.lower(), symbol
        # ble_store_config_init is the legitimate SDK RAM store initializer.
        # The persistence object, not that shared name, must be absent.
        link_map = (OUT / name / 'firmware.map').read_text()
        assert 'ble_store_nvs.c.obj' not in link_map
        assert all(s in symbols for s in ['wifi_setup_runtime', 'ble_store_config_conf_init'])
        # This toolchain garbage-collects both erase wrappers when there are no
        # live callers. Require wrapper provenance plus absence of real erase
        # entrypoints, rather than retaining dead code just for an ELF check.
        for function in ['nvs_flash_erase', 'nvs_flash_erase_partition', 'nvs_flash_erase_partition_ptr']:
            assert not re.search(r' [Tt] ' + function + r'$', symbols, re.M)
        for function in ['nvs_flash_erase', 'nvs_flash_erase_partition']:
            assert '__wrap_' + function in link_map
            assert '-Wl,--wrap=' + function in (ROOT / 'platformio.ini').read_text()
    frames = []
    for path in (ROOT / '.pio/build' / env).rglob('*.su'):
        for line in path.read_text().splitlines():
            fields = line.split('\t')
            if len(fields) >= 2 and fields[1].isdigit():
                frames.append(dict(function=fields[0], bytes=int(fields[1]),
                    kind=fields[2] if len(fields) > 2 else '', file=str(path.relative_to(ROOT))))
    (OUT / (name + '-stack-usage.json')).write_text(json.dumps(frames, indent=2) + '\n')
    data['largest_stack_frames'] = sorted(frames, key=lambda f: f['bytes'], reverse=True)[:30]
    report[name] = data
assert len({d['partition_sha256'] for d in report.values()}) == 1
assert report['m1']['partition_sha256'] == '35897a52e0d951707f219eb6292b945ad55095364e3a8075eeeb4b221e211798'
recovery = ROOT / 'artifacts/controller-bootstrap/isolated/firmware.bin'
assert hashlib.sha256(recovery.read_bytes()).hexdigest() == 'f511330c690fae98b32555ab7fc4c94499adf576bc995a62a84381ae1080c136'
report['recovery_app_reverified'] = True
report['scope'] = 'Static target ELF, section and individual stack frames only; dynamic heap, full call-chain stack, RF/USB remain unverified.'
(OUT / 'link-memory-audit.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS artifact hashes, unchanged partitions, production feature exclusion, M2 worker/persistent-BLE-store exclusion and preserved prep2 recovery hash')
