"""Validate saved read-only LOG pages and symbolize them against an exact ELF.

Offline only: never opens a serial port, resets, flashes, or contacts a service.
No heap payloads, credentials or files from the microSD are inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

FIELDS = {'address', 'caller', 'bytes', 'sequence', 'phase', 'api', 'live'}
STABLE = ['snapshot', 'phase', 'pages', 'at_stop', 'still_live', 'released_later',
          'live_bytes', 'overflow', 'failed_allocations', 'denied_op',
          'denied_caller', 'denied_phase', 'settle_ms']


def integer(value, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError('Invalid numeric trace field')
    return value


def program_counter(value):
    value = integer(value, 1, 0xffffffff)
    # Xtensa windowed return-address encodings can carry call-window bits.
    if value & 0x80000000:
        value = 0x40000000 | (value & 0x3fffffff)
    if not 0x40000000 <= value < 0x44000000:
        raise ValueError('Caller is outside expected ESP32-S3 code ranges')
    return value

def validate_heap_span(span):
    fields={'captured','overflow','heap_start','heap_end','before_start','before_bytes',
            'after_start','after_bytes','global_after_start','global_after_bytes','barriers_count','barriers'}
    if not isinstance(span,dict) or set(span)!=fields or span['captured'] is not True:
        raise ValueError('Missing immutable raw heap span')
    for k in fields-{'captured','barriers'}:integer(span[k],0,0xffffffff)
    lo,hi,start,size=(span[k] for k in ['heap_start','heap_end','before_start','before_bytes'])
    if not lo<=start<hi or not size or size>hi-start:
        raise ValueError('Invalid baseline heap range')
    rows=span['barriers']
    if not isinstance(rows,list) or len(rows)!=span['barriers_count'] or len(rows)>16:
        raise ValueError('Invalid retained-block count')
    addresses=set()
    for row in rows:
        if not isinstance(row,dict) or set(row)!={'address','bytes'}:
            raise ValueError('Heap payload fields are forbidden')
        address=integer(row['address'],lo,hi-1)
        length=integer(row['bytes'],1,hi-address)
        if address>=start+size or address+length<=start:
            raise ValueError('Retained block does not intersect baseline span')
        if address in addresses:raise ValueError('Duplicate retained block')
        addresses.add(address)
    for a,n in [('after_start','after_bytes'),('global_after_start','global_after_bytes')]:
        if span[n]>0xffffffff-span[a]:raise ValueError('Overflowing free span')
    return span


def validate_pages(pages, expected_firmware='0.3.3-ble-m1c-trace1'):
    if expected_firmware not in {'0.3.3-ble-m1c-trace1','0.3.3-ble-m1c-trace2','0.3.3-ble-m1c-trace3'}:
        raise ValueError('Unknown reviewed trace protocol version')
    if not isinstance(pages, list) or not 1 <= len(pages) <= 22:
        raise ValueError('Expected 1..22 bounded LOG pages')
    rows, by_page, reference = [], {}, None
    for page in pages:
        if not isinstance(page, dict) or len(json.dumps(page).encode()) > 8192:
            raise ValueError('Invalid or oversized USB reply')
        if page.get('firmware') != expected_firmware:
            raise ValueError('Wrong firmware; do not mix diagnostic images')
        t = page.get('alloc_trace')
        if not isinstance(t, dict) or any(k not in t for k in STABLE):
            raise ValueError('Missing immutable trace metadata')
        stable = {k: t[k] for k in STABLE}
        if reference is None:
            reference = stable
        if stable != reference or t['phase'] != 5:
            raise ValueError('Mixed snapshots or trace not frozen')
        count = integer(t['pages'], 1, 22)
        index = integer(t.get('page'), 0, count - 1)
        if index in by_page:
            raise ValueError('Duplicate trace page')
        values = t.get('records')
        if not isinstance(values, list) or len(values) > 12:
            raise ValueError('Invalid page record count')
        by_page[index] = values
    assert reference is not None
    count = reference['pages']
    if set(by_page) != set(range(count)):
        raise ValueError('Incomplete trace snapshot')
    integer(reference['snapshot'], 1, 0xffffffff)
    total = integer(reference['at_stop'], 0, 256)
    integer(reference['still_live'], 0, 256)
    integer(reference['released_later'], 0, 256)
    integer(reference['live_bytes'], 0, 256*0xffffffff)
    if count != max(1, (total + 11) // 12):
        raise ValueError('Inconsistent page count')
    for index in range(count):
        values = by_page[index]
        if len(values) != min(12, max(0, total - index * 12)):
            raise ValueError('Truncated or reordered page payload')
        rows.extend(values)
    addresses, sequences = set(), set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != FIELDS or type(row['live']) is not bool:
            raise ValueError('Unexpected record fields; payload data is prohibited')
        address = integer(row['address'], 1, 0xffffffff)
        sequence = integer(row['sequence'], 1, 0xffffffff)
        if address in addresses or sequence in sequences:
            raise ValueError('Duplicate allocation identity')
        addresses.add(address); sequences.add(sequence)
        program_counter(row['caller'])
        integer(row['bytes'], 0, 0xffffffff)
        integer(row['phase'], 1, 3); integer(row['api'], 1, 7)
    live = [r for r in rows if r['live']]
    if (len(rows) != total or reference['still_live'] != len(live)
            or reference['released_later'] != total - len(live)
            or reference['live_bytes'] != sum(r['bytes'] for r in live)):
        raise ValueError('Inconsistent allocation totals')
    integer(reference['overflow'], 0, 0xffffffff)
    integer(reference['failed_allocations'], 0, 0xffffffff)
    if reference['settle_ms'] != 1000:
        raise ValueError('Unexpected observation interval')
    op = integer(reference['denied_op'], 0, 7)
    integer(reference['denied_phase'], 0, 5)
    if op:
        program_counter(reference['denied_caller'])
    elif reference['denied_caller'] != 0:
        raise ValueError('Unexpected denied caller without an operation')
    result={'summary': reference, 'records': rows,
            'bounded_table_complete': reference['overflow'] == 0,
            'scope': 'Wrapped allocation paths only; same-object, ROM and unwrapped allocation/free paths may be absent.'}
    if expected_firmware.endswith(('trace2','trace3')):
        span=validate_heap_span(pages[0].get('heap_span'))
        if any(p.get('heap_span')!=span for p in pages):
            raise ValueError('Mixed raw heap snapshots')
        result['heap_span']=span
        result['bounded_table_complete'] &= span['overflow']==0
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--elf-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--addr2line', type=Path)
    parser.add_argument('--firmware', choices=['0.3.3-ble-m1c-trace1','0.3.3-ble-m1c-trace2','0.3.3-ble-m1c-trace3'],
                        default='0.3.3-ble-m1c-trace1')
    args = parser.parse_args()
    if args.output.resolve() in {args.input.resolve(), args.elf.resolve()}:
        raise ValueError('Output must preserve both the input evidence and ELF')
    if not re.fullmatch(r'[0-9a-f]{64}', args.elf_sha256):
        raise ValueError('An exact reviewed ELF hash is required')
    elf_bytes=args.elf.read_bytes()
    actual = hashlib.sha256(elf_bytes).hexdigest()
    if actual != args.elf_sha256:
        raise ValueError('ELF hash mismatch; refusing misleading symbolization')
    if args.firmware.encode()+b'\x00' not in elf_bytes:
        raise ValueError('ELF firmware identity mismatch; refusing cross-version symbols')
    if args.input.stat().st_size > 256*1024:
        raise ValueError('Saved trace exceeds the bounded snapshot size')
    data = json.loads(args.input.read_text())
    result = validate_pages(data, args.firmware)
    core = Path(os.environ.get('PLATFORMIO_CORE_DIR', '/config/.unmanic/research/cloud-stick-platformio'))
    tool = args.addr2line or core/'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-addr2line'
    callers = {program_counter(r['caller']) for r in result['records']}
    if result['summary']['denied_op']:
        callers.add(program_counter(result['summary']['denied_caller']))
    symbols = {}
    for pc in sorted(callers):
        run = subprocess.run([str(tool), '-a', '-f', '-C', '-i', '-e', str(args.elf), hex(pc)],
                             capture_output=True, text=True, check=True, timeout=10)
        if run.stderr:
            raise ValueError('Symbolizer reported a diagnostic')
        symbols[hex(pc)] = run.stdout.splitlines()
    result.update({'elf_sha256': actual, 'symbols': symbols,
                   'warning': 'Live at the 1-second snapshot is not proof of a leak; SDK globals may be intentionally retained.'})
    with args.output.open('x') as output:
        output.write(json.dumps(result, indent=2)+'\n')
    print('Validated', len(result['records']), 'records; wrote offline symbolization.')


if __name__ == '__main__':
    main()
