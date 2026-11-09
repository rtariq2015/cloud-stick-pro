"""Full coordinator tests use actual signed package/crypto and a fake ROM.

Run in a separate process because the private worker suppresses dependency stdio
and defers termination. No serial port, radio, live settings or device is used.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest
from cloudstick_desktop.firmware_catalog import load_catalog

DESKTOP = Path(__file__).resolve().parents[1]
PACKAGE = DESKTOP / 'firmware' / load_catalog((DESKTOP/'firmware/catalog.json').read_bytes()).latest.asset
CHILD = r'''
import contextlib,json,os,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from cloudstick_desktop import firmware_worker as w,firmware_engine as e,firmware_recovery as r,firmware_usb as u
from cloudstick_desktop.firmware_catalog import load_catalog
root=Path(sys.argv[2]);scenario=sys.argv[3]
root.mkdir(mode=0o700)
job=root/('a'*32);job.mkdir(mode=0o700)
fp=[0x303a,0x1001,'112233445566','SYNTHETIC-USB-LOCATION']
config={'port':'COM3','fingerprint':fp,'mode':'new','resume':False,'prepared':None}
r.save_private(job/'job.json',json.dumps(config).encode())
firmware=Path(sys.argv[1])/'firmware'
catalog=(firmware/'catalog.json').read_bytes();latest=load_catalog(catalog).latest
r.save_private(job/'catalog.json',catalog)
r.save_private(job/'package.cspfw',(firmware/latest.asset).read_bytes())
w.data_root=lambda:root
w.require_no_swap=lambda:None
r.require_no_swap=lambda:None # explicit synthetic-only fixture; never real flash
w.ensure_rom_storage_absent=lambda _:None
w.operation_lock=lambda _:contextlib.nullcontext()
w.time.sleep=lambda _:None
clock=[0]
def tick(): clock[0]+=10;return clock[0]
w.time.monotonic=tick
memory=bytearray(b'\xff'*0x1000000)
counts={'reads':0,'writes':0,'boots':0}
class Device:
 mac='112233445566'
 def __init__(self,*_): pass
 def prove(self): pass
 def close(self): pass
 def read(self,offset,size):
  counts['reads']+=1
  data=bytes(memory[offset:offset+size])
  if scenario=='mismatch' and counts['reads']==257:return b'x'*size
  return data
 def write(self,plan):
  counts['writes']+=1
  if scenario in ('resume','wrong-board') and counts['writes']==1:
   start,data=plan[0];memory[start:start+4096]=data[:4096]
   raise OSError('SYNTHETIC disconnect with private-looking text never exported')
  for start,data in plan:memory[start:start+len(data)]=data
 def normal_boot(self):counts['boots']+=1
u.ports=lambda:{'COM3':tuple(fp)}
u.normal_status=lambda *_:[{'firmware':'unexpected' if scenario=='boot-fault' else latest.version,'store_fault':False,'saved_revision':'0'}, {'owner_state':1}]
e.ROMDevice=Device
first=w.run(job)
initial=json.loads((job/'progress.json').read_bytes())
reads_before=counts['reads']
second=None
if scenario in ('resume','wrong-board'):
 config['resume']=True;r.save_private(job/'job.json',json.dumps(config).encode())
 if scenario=='wrong-board':Device.mac='aabbccddeeff'
 second=w.run(job)
final=json.loads((job/'progress.json').read_bytes())
(root.parent/'result.json').write_text(json.dumps({'first':first,'second':second,'initial':initial,'final':final,'counts':counts,'resume_reads':counts['reads']-reads_before}))
'''


@unittest.skipUnless(PACKAGE.is_file(), 'Full-worker tests need reviewed offline data; native CI fetches/verifies it first')
class WorkerTests(unittest.TestCase):
    def scenario(self, name):
        with tempfile.TemporaryDirectory(prefix='csp-synthetic-coordinator-') as temporary:
            root = Path(temporary)
            script = root / 'child.py'; script.write_text(textwrap.dedent(CHILD))
            completed = subprocess.run([sys.executable, str(script), str(DESKTOP), str(root / 'private'), name],
                                       capture_output=True, timeout=90)
            self.assertEqual(completed.returncode, 0, 'Synthetic coordinator subprocess failed')
            result = json.loads((root / 'result.json').read_bytes())
            self.assertNotIn('private-looking', json.dumps(result))
            return result

    def test_new_board_full_pipeline_and_postboot_check(self):
        result = self.scenario('normal')
        self.assertEqual(result['first'], 0)
        self.assertEqual(result['final']['phase'], 'installed')
        self.assertEqual(result['counts'], {'reads': 768, 'writes': 1, 'boots': 1})

    def test_interrupted_write_only_resumes_after_explicit_request(self):
        result = self.scenario('resume')
        self.assertEqual(result['first'], 1)
        self.assertEqual(result['initial']['phase'], 'recovery_required')
        self.assertTrue(result['initial']['writing'])
        self.assertEqual(result['second'], 0)
        self.assertEqual(result['final']['phase'], 'installed')
        self.assertEqual(result['counts']['writes'], 2)
        self.assertEqual(result['counts']['boots'], 1)

    def test_wrong_board_resume_refused_before_reading_its_flash(self):
        result = self.scenario('wrong-board')
        self.assertEqual(result['second'], 1)
        self.assertEqual(result['resume_reads'], 0)
        self.assertEqual(result['counts']['writes'], 1)
        self.assertEqual(result['counts']['boots'], 0)
        self.assertEqual(result['final']['phase'], 'recovery_required')

    def test_nonmatching_backup_reads_never_write_or_reset(self):
        result = self.scenario('mismatch')
        self.assertEqual(result['first'], 1)
        self.assertEqual(result['counts']['writes'], 0)
        self.assertEqual(result['counts']['boots'], 0)
        self.assertEqual(result['final']['phase'], 'failed')
        self.assertFalse(result['final']['writing'])

    def test_verified_flash_without_postboot_proof_needs_review(self):
        result = self.scenario('boot-fault')
        self.assertEqual(result['first'], 1)
        self.assertEqual(result['counts']['writes'], 1)
        self.assertEqual(result['counts']['boots'], 1)
        self.assertEqual(result['final']['phase'], 'boot_review')
        self.assertEqual(result['final']['code'], 'firmware_boot_check_failed')


if __name__ == '__main__': unittest.main()
