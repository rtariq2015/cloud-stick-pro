#!/usr/bin/env python3
"""Actual phone dispatcher/app tests with synthetic data only."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/event-log-phone'
SUITES={
 'activity-and-txt':['test/event_log_phone/protocol.test.mjs','test/event_log_phone/ui.test.mjs','test/event_log_phone/native_runner.test.mjs'],
 'public-wifi':['test/public_wifi_phone/protocol.test.mjs','test/public_wifi_phone/ui.test.mjs'],
 'existing-phone':['test/wifi_integration/phone-ui.test.mjs','test/wifi_integration/scan_status.test.mjs',
                   'test/backup_integration/phone_backup_ui.test.mjs','test/backup_integration/phone_backup_admission.test.mjs','test/backup_integration/backup_wording_ui.test.mjs'],
}
def hashes():
 paths=[ROOT/'phone/wifi-setup'/name for name in ['index.html','style.css','app.mjs','connection.mjs','security2.mjs']]
 paths+=list((ROOT/'test/event_log_phone').glob('*.mjs'))
 paths+=[ROOT/'tools/native_ble/run.mjs']
 return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}
def main():
 OUT.mkdir(parents=True,exist_ok=True);before=hashes();runs=[]
 for name,files in SUITES.items():
  command=['node','--test','--test-reporter=tap',*files];result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=60)
  log=OUT/(name+'.tap');log.write_text(result.stdout+result.stderr)
  totals={k:int(v) for k,v in re.findall(r'^# (tests|pass|fail|cancelled|skipped|todo) (\d+)$',result.stdout,re.M)}
  passed=result.returncode==0 and not result.stderr and totals.get('tests',0)>0 and totals.get('tests')==totals.get('pass') and all(totals.get(k)==0 for k in ['fail','cancelled','skipped','todo'])
  runs.append({'name':name,'command':command,'exit':result.returncode,'stderr_empty':not result.stderr,'totals':totals,'pass':passed,'log':str(log.relative_to(ROOT)),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
  print(('PASS' if passed else 'FAIL'),name,totals,flush=True)
 after=hashes();record={'pass':before==after and all(r['pass'] for r in runs),'source_stable':before==after,'source_sha256':after,'runs':runs,
  'scope':'Actual ManagementConnection + DiagnosticLogClient + app; DOM/GATT/transparent security fixture simulated; local synthetic TXT/JSON inputs',
  'physical_radio':False,'private_files':False,'cloud_contact':False,'website_contact':False,'cryptographic_interoperability_claim':False}
 (OUT/'results.json').write_text(json.dumps(record,indent=2)+'\n')
 if not record['pass']:raise RuntimeError('Phone gate failed; inspect '+str(OUT))
if __name__=='__main__':main()
