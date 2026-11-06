"""Offline hostile/synthetic trace-export validation; no device or serial use."""
import copy
import importlib.util
from pathlib import Path
import unittest
import sys

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('trace_symbolizer',ROOT/'tools/symbolize_ble_allocations.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


def fixture(count=25):
    records=[{'address':0x3fc90000+i*32,'caller':0x42001000+i*8,
              'bytes':32,'sequence':i+1,'phase':2,'api':1,'live':i%2==0} for i in range(count)]
    live=[r for r in records if r['live']]
    pages=max(1,(count+11)//12)
    base={'snapshot':1,'phase':5,'pages':pages,'at_stop':count,'still_live':len(live),
          'released_later':count-len(live),'live_bytes':32*len(live),'overflow':0,
          'failed_allocations':0,'denied_op':1,'denied_caller':0x42002000,
          'denied_phase':1,'settle_ms':1000}
    return [{'firmware':'0.3.3-ble-m1c-trace1','alloc_trace':dict(base,page=i,records=records[i*12:(i+1)*12])}
            for i in range(pages)]

def span_fixture():
    return {'captured':True,'overflow':0,'heap_start':0x3fc90000,'heap_end':0x3fcd0000,
            'before_start':0x3fc91000,'before_bytes':0x30000,'after_start':0x3fc99000,
            'after_bytes':0x28000,'global_after_start':0x3fc99000,'global_after_bytes':0x28000,
            'barriers_count':1,'barriers':[{'address':0x3fc98fe8,'bytes':16}]}


class Pages(unittest.TestCase):
    def test_explicit_trace2_and_no_version_mixing(self):
        for version in ['0.3.3-ble-m1c-trace2','0.3.3-ble-m1c-trace3']:
            p=fixture(25)
            for page in p:
                page['firmware']=version;page['heap_span']=span_fixture()
            with self.assertRaises(ValueError):module.validate_pages(p)
            self.assertEqual(len(module.validate_pages(p,version)['records']),25)
            p[0]['firmware']='0.3.3-ble-m1c-trace1'
            with self.assertRaises(ValueError):module.validate_pages(p,version)

    def test_heap_span_bounds_payload_and_completeness(self):
        p=fixture(25)
        for page in p:page.update(firmware='0.3.3-ble-m1c-trace2',heap_span=span_fixture())
        r=module.validate_pages(p,'0.3.3-ble-m1c-trace2')
        self.assertEqual(r['heap_span']['barriers'][0]['bytes'],16)
        for key,value in [('captured',False),('barriers_count',True),('before_bytes',0xffffffff),
                          ('heap_end',0),('payload','prohibited'),('after_start',0xffffffff)]:
            bad=copy.deepcopy(p)
            for page in bad:page['heap_span'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):module.validate_pages(bad,'0.3.3-ble-m1c-trace2')
        bad=copy.deepcopy(p);bad[1]['heap_span']['overflow']=1
        with self.assertRaises(ValueError):module.validate_pages(bad,'0.3.3-ble-m1c-trace2')
        for page in p:page['heap_span']['overflow']=1
        self.assertFalse(module.validate_pages(p,'0.3.3-ble-m1c-trace2')['bounded_table_complete'])

    def test_complete_shuffled_and_empty_snapshots(self):
        for n in [0,1,12,13,25,256]:
            pages=fixture(n);pages.reverse();r=module.validate_pages(pages)
            self.assertEqual(len(r['records']),n);self.assertTrue(r['bounded_table_complete'])

    def test_missing_duplicate_mixed_unfrozen(self):
        mutations=[lambda p:p.pop(),lambda p:p.append(copy.deepcopy(p[0])),
                   lambda p:p[1]['alloc_trace'].update(snapshot=2),
                   lambda p:p[0]['alloc_trace'].update(phase=4),
                   lambda p:p[0].update(firmware='wrong-version'),
                   lambda p:p[1]['alloc_trace']['records'].pop(),
                   lambda p:p[0]['alloc_trace'].update(pages=99)]
        for change in mutations:
            with self.subTest(change=change):
                p=fixture();change(p)
                with self.assertRaises(ValueError):module.validate_pages(p)

    def test_hostile_records(self):
        for key,value in [('caller',0),('caller',0x12345678),('caller',True),
                          ('bytes',-1),('bytes',True),('sequence',0),('api',8),
                          ('phase',4),('live',1),('password','prohibited-field')]:
            with self.subTest(key=key,value=value):
                p=fixture(1);p[0]['alloc_trace']['records'][0][key]=value
                with self.assertRaises(ValueError):module.validate_pages(p)

    def test_totals_and_types(self):
        for key,value in [('at_stop',True),('still_live',True),('released_later',False),
                          ('live_bytes',True),('live_bytes',-1),('settle_ms',5),
                          ('denied_op',8),('denied_caller',0),('overflow',-1)]:
            p=fixture(1);p[0]['alloc_trace'][key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):module.validate_pages(p)

    def test_overflow_is_reported_as_incomplete(self):
        p=fixture(1);p[0]['alloc_trace']['overflow']=1
        self.assertFalse(module.validate_pages(p)['bounded_table_complete'])

    def test_encoded_pc_and_oversize_reply(self):
        self.assertEqual(module.program_counter(0x82001234),0x42001234)
        p=fixture(1);p[0]['padding']='x'*8192
        with self.assertRaises(ValueError):module.validate_pages(p)


if __name__=='__main__':unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))
