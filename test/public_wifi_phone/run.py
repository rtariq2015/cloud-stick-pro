#!/usr/bin/env python3
"""Bounded source-only phone regressions; no devices, accounts or network."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/public-wifi/phone'
SUITES={
 'public-wifi-phone':['test/public_wifi_phone/protocol.test.mjs','test/public_wifi_phone/ui.test.mjs'],
 'existing-phone':['test/wifi_integration/phone-ui.test.mjs','test/wifi_integration/scan_status.test.mjs',
                   'test/backup_integration/phone_backup_ui.test.mjs','test/backup_integration/phone_backup_admission.test.mjs',
                   'test/backup_integration/backup_wording_ui.test.mjs'],
}
def hashes():
 paths=[ROOT/'phone/wifi-setup'/name for name in ['index.html','style.css','app.mjs','connection.mjs','security2.mjs']]
 paths+=list((ROOT/'test/public_wifi_phone').glob('*.mjs'))
 return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 before=hashes();runs=[]
 for name,files in SUITES.items():
  command=['node','--test','--test-reporter=tap',*files]
  result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=60)
  logfile=OUT/(name+'.tap');logfile.write_text(result.stdout+result.stderr)
  totals={key:int(value) for key,value in re.findall(r'^# (tests|pass|fail|cancelled|skipped|todo) (\d+)$',result.stdout,re.M)}
  passed=result.returncode==0 and not result.stderr and totals.get('tests',0)>0 and totals.get('pass')==totals.get('tests') and all(totals.get(key)==0 for key in ['fail','cancelled','skipped','todo'])
  runs.append({'name':name,'command':command,'exit':result.returncode,'stderr_empty':not result.stderr,'totals':totals,'pass':passed,
               'log':str(logfile.relative_to(ROOT)),'log_sha256':hashlib.sha256(logfile.read_bytes()).hexdigest()})
  print(('PASS' if passed else 'FAIL'),name,totals,flush=True)
 after=hashes();stable=before==after
 record={'scope':'Actual ManagementConnection dispatcher + PublicWifiClient + app, synthetic DOM/GATT/transparent test security only',
         'physical_radio':False,'live_cloud':False,'website_contact':False,'cryptographic_interoperability_claim':False,
         'source_stable_during_tests':stable,'source_sha256':after,'runs':runs,
         'pass':stable and all(run['pass'] for run in runs)}
 (OUT/'results.json').write_text(json.dumps(record,indent=2)+'\n')
 if not record['pass']:sys.exit(1)
if __name__=='__main__':main()
