"""Offline ELF audit of the diagnostic observer's ownership and RAM placement."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--env',default='tdongle_s3_ble_m1_trace');parser.add_argument('--disabled',action='store_true')
parser.add_argument('--without-denial-export',action='store_true',help='Bench observer does not export the isolated flash-denial details; require their getter to be absent')
parser.add_argument('--network-hooks',action='store_true',help='Require the expanded bench-only newlib/lwIP/default allocator hooks')
args=parser.parse_args()
core=Path(os.environ.get('PLATFORMIO_CORE_DIR','/config/.unmanic/research/cloud-stick-platformio'))
tool=str(core/'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-')
build=ROOT/'.pio/build'/args.env
elf=build/'firmware.elf'
table=subprocess.check_output([tool+'objdump','-t',str(elf)],text=True)
symbols={}
for line in table.splitlines():
    parts=line.split()
    if len(parts)>=5 and len(parts[0])==8:
        try: address=int(parts[0],16)
        except ValueError:continue
        symbols[parts[-1]]={'address':address,'section':parts[-3]}
wrappers=['malloc','calloc','realloc','free','heap_caps_malloc','heap_caps_calloc',
          'heap_caps_realloc','heap_caps_free','heap_caps_aligned_alloc','heap_caps_aligned_calloc']
if args.network_hooks:
    wrappers+=['heap_caps_malloc_default','_malloc_r','_calloc_r','_realloc_r','_free_r','mem_malloc','mem_free','memp_malloc','memp_free',
               'multi_heap_malloc','multi_heap_realloc','multi_heap_free','multi_heap_aligned_alloc_offs','esp_timer_create']
result={'environment':args.env,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),
        'physical_test':False,'scope':'ELF normal-path placement only; not target timing/ISR/cache-off acceptance'}
if args.disabled:
    assert not any('__wrap_'+name in symbols for name in wrappers)
    assert not any('ble_alloc_trace' in name for name in symbols)
    result['trace_absent']=True
else:
    for name in wrappers:
        assert symbols['__wrap_'+name]['section']=='.iram0.text',name
    data={k:v for k,v in symbols.items() if any(s in k for s in ['_17trackerE','_13muxE','_15armedE','_111firstDenialE','_115denialPublishedE','_18heapSpanE'])}
    if args.without_denial_export:
        assert not any('ble_alloc_trace_denied_call' in name for name in symbols)
        assert not any('firstDenial' in name for name in symbols)
        assert len(data)==5,data
        result['denial_detail_export_absent']=True
    else:assert len(data)==6,data
    assert all(v['section'].startswith('.dram0.') for v in data.values()),data
    observers={k:v for k,v in symbols.items() if any(s in k for s in ['_19allocatedEP','_18releasedEP','_14takeEP','_111reallocDoneE','_13GuardC','_13GuardD','_Z22ble_alloc_trace_denial'])}
    assert len(observers)>=(4 if args.without_denial_export else 5),observers
    for core_observer in ['_19allocatedEP','_18releasedEP','_14takeEP','_111reallocDoneE']:
        assert any(core_observer in name for name in observers),core_observer
    if args.network_hooks:
        annotations={k:v for k,v in symbols.items() if '_19annotatedEP' in k}
        # Optimized target builds inline this small observer into the audited
        # IRAM memp wrapper. If emitted separately, require IRAM placement too.
        assert all(v['section']=='.iram0.text' for v in annotations.values()),annotations
        observers.update(annotations)
    assert all(v['section']=='.iram0.text' for v in observers.values()),observers
    obj=build/'src/ble_alloc_trace.cpp.o'
    undefined=subprocess.check_output([tool+'nm','-u',str(obj)],text=True).splitlines()
    dependencies={}
    relocation=subprocess.check_output([tool+'objdump','-r',str(obj)],text=True)
    section=''
    for line in relocation.splitlines():
        if line.startswith('RELOCATION RECORDS FOR ['):section=line
        if section.startswith('RELOCATION RECORDS FOR [.iram'):
            assert 'heap_caps_walk' not in line,'Heap walking entered an allocator/ISR hook'
    for line in undefined:
        name=line.split()[-1]
        if name.startswith('__real_') or name=='__gxx_personality_v0':continue
        # A public owner-task-only walker added for raw-span snapshots. Its
        # references are checked above to be absent from all IRAM hook sections.
        if name=='heap_caps_walk':continue
        s=symbols[name]
        assert s['section']=='.iram0.text' or 0x40000000<=s['address']<0x40080000,(name,s)
        dependencies[name]=s
    result.update({'wrapper_count':len(wrappers),'wrappers_in_iram':True,'observer_functions':observers,
                   'metadata_in_internal_dram':data,'observer_dependencies':dependencies,
                   'owner_heap_walk_absent_from_iram_hooks':True,
                   'caveat':'Original allocators retain their SDK restrictions. C++ exception metadata is not a normal hook call path; no hook intentionally throws.'})
result['passed']=True
out=ROOT/'artifacts/allocation-diagnostics';out.mkdir(exist_ok=True)
(out/(args.env+'-link.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:result[k] for k in ['environment','passed','wrapper_count','trace_absent'] if k in result}))
