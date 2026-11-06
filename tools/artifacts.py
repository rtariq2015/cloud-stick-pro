"""Package local build outputs; no network, device access, or flashing."""
import argparse
import hashlib
import json
import os
import re
import shutil
from pathlib import Path
from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser()
parser.add_argument('--baseline', type=Path)
parser.add_argument('--out', type=Path, help='Separate artifact directory for this firmware version')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = root / '.pio/build/tdongle_s3'
out = args.out or root / 'artifacts'
out.mkdir(exist_ok=True)

def measure(path):
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        sections = {s.name: {'bytes': s['sh_size'], 'address': hex(s['sh_addr']), 'flags': s['sh_flags']} for s in elf.iter_sections() if s['sh_flags'] & 2}
    return {'elf_file_bytes': path.stat().st_size, 'sections': sections}

report = {'pro': measure(build/'firmware.elf')}
if args.baseline:
    report['baseline'] = measure(args.baseline)
stack = []
for file in (build/'src').glob('*.su'):
    for line in file.read_text().splitlines():
        fields = line.split('\t')
        if len(fields) == 3:
            stack.append({'function': fields[0], 'bytes': int(fields[1]), 'kind': fields[2]})
report['compiler_function_stacks'] = sorted(stack, key=lambda s: s['bytes'], reverse=True)
for label in ('pro', 'baseline'):
    log = out / (label+'-build.log')
    if log.exists() and label in report:
        text = log.read_text()
        for field, pattern in [('pio_static_ram', r'RAM:.*used (\d+) bytes'), ('pio_program_space', r'Flash:.*used (\d+) bytes')]:
            values = re.findall(pattern, text)
            if values:
                report[label][field] = int(values[-1])
for name in ('firmware.bin', 'bootloader.bin', 'partitions.bin', 'littlefs.bin', 'firmware.elf', 'firmware.map'):
    shutil.copy2(build/name, out/name)
factory = (build/'firmware.factory.bin').read_bytes()
metadata = (build/'littlefs.bin').read_bytes()
if len(factory) > 0x410000 or len(metadata) != 0x100000:
    raise SystemExit('Image layout differs from reviewed partitions')
merged = bytearray(b'\xff' * 0x510000)
merged[:len(factory)] = factory
merged[0x410000:] = metadata
(out/'cloud-stick-pro.factory.bin').write_bytes(merged)
# Preserve the OTA-data initializer for users choosing individual images.
source = Path(os.environ.get('PLATFORMIO_CORE_DIR',str(Path.home()/'.platformio'))) / 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
if source.exists():
    shutil.copy2(source, out/'boot_app0.bin')
report['artifact_bytes'] = {p.name: p.stat().st_size for p in out.iterdir() if p.suffix in ('.bin', '.elf', '.map')}
(out/'sizes.json').write_text(json.dumps(report, indent=2)+'\n')
sums=[]
for file in sorted(out.iterdir()):
    if file.name == 'SHA256SUMS' or file.suffix == '.o' or file.name == 'native-tests':
        continue
    if file.is_file():
        sums.append(hashlib.sha256(file.read_bytes()).hexdigest()+'  '+file.name)
(out/'SHA256SUMS').write_text('\n'.join(sums)+'\n')
print(json.dumps({k: {f:v for f,v in value.items() if f != 'sections'} for k,value in report.items() if k in ('pro','baseline')}, indent=2))
print('Packaged', len(sums), 'artifacts; factory image',len(merged),'bytes')
