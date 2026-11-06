"""Offline CLI roundtrip against the reviewed target ELF; never uses hardware."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts/allocation-diagnostics'
ELF = ROOT / '.pio/build/tdongle_s3_ble_m1_trace/firmware.elf'
CORE = Path(os.environ.get('PLATFORMIO_CORE_DIR', '/config/.unmanic/research/cloud-stick-platformio'))
BIN = CORE / 'packages/toolchain-xtensa-esp-elf/bin'
digest = hashlib.sha256(ELF.read_bytes()).hexdigest()
symbols = subprocess.run([str(BIN/'xtensa-esp-elf-nm'), '--defined-only', str(ELF)],
                         capture_output=True, text=True, check=True, timeout=30)
assert not symbols.stderr
pc = next(int(line.split()[0], 16) for line in symbols.stdout.splitlines()
          if line.split()[-1] == '__wrap_malloc')
spec = importlib.util.spec_from_file_location('page_tests', ROOT/'test/test_ble_trace_pages.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
pages = fixture.fixture(1)
pages[0]['firmware']='0.3.3-ble-m1c-trace2'
pages[0]['heap_span']=fixture.span_fixture()
pages[0]['alloc_trace']['records'][0]['caller'] = pc
pages[0]['alloc_trace'].update(denied_op=0, denied_caller=0, denied_phase=0)
results = []
with tempfile.TemporaryDirectory(prefix='offline-trace-') as folder:
    folder = Path(folder)
    source, target = folder/'pages.json', folder/'symbols.json'
    source.write_text(json.dumps(pages))
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    args = ['python3', 'tools/symbolize_ble_allocations.py', '--input', str(source),
            '--elf', str(ELF), '--elf-sha256', digest, '--output', str(target),
            '--firmware','0.3.3-ble-m1c-trace2']
    run = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=30)
    assert run.returncode == 0 and not run.stderr, run.stderr
    decoded = json.loads(target.read_text())
    assert decoded['elf_sha256'] == digest
    assert '__wrap_malloc' in '\n'.join(decoded['symbols'][hex(pc)])
    results.append('Exact target ELF resolves synthetic caller to __wrap_malloc')
    # Existing evidence is never overwritten, even with an otherwise valid input.
    previous = target.read_bytes()
    run = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=30)
    assert run.returncode != 0 and target.read_bytes() == previous
    results.append('Existing output preserved')
    bad = args.copy()
    bad[bad.index('--elf-sha256')+1] = '0'*64
    bad[bad.index('--output')+1] = str(folder/'must-not-exist.json')
    run = subprocess.run(bad, cwd=ROOT, capture_output=True, text=True, timeout=30)
    assert run.returncode != 0 and 'ELF hash mismatch' in run.stderr
    assert not (folder/'must-not-exist.json').exists()
    results.append('Wrong ELF hash rejected before output')
    bad=args.copy();bad[bad.index('--firmware')+1]='0.3.3-ble-m1c-trace1'
    bad[bad.index('--output')+1]=str(folder/'cross-version.json')
    run=subprocess.run(bad,cwd=ROOT,capture_output=True,text=True,timeout=30)
    assert run.returncode!=0 and 'ELF firmware identity mismatch' in run.stderr
    assert not (folder/'cross-version.json').exists()
    results.append('Correct hash with wrong firmware identity rejected')
    for protected in [source, ELF]:
        bad = args.copy()
        bad[bad.index('--output')+1] = str(protected)
        run = subprocess.run(bad, cwd=ROOT, capture_output=True, text=True, timeout=30)
        assert run.returncode != 0 and 'Output must preserve' in run.stderr
    assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash
    assert hashlib.sha256(ELF.read_bytes()).hexdigest() == digest
    results.append('Input/ELF overwrite denied and both hashes preserved')
(OUT/'symbolizer-cli-tests.json').write_text(json.dumps({
    'passed': True, 'physical_device_access': False, 'elf_sha256': digest,
    'tests': results}, indent=2)+'\n')
print('PASS offline symbolizer CLI:', len(results), 'checks; no device access')
