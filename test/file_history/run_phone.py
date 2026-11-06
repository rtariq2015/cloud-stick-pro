#!/usr/bin/env python3
"""Actual app/dispatcher/history parsing, synthetic DOM/GATT only."""
from pathlib import Path
import hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'artifacts/file-history-phone'
def hashes():
 paths=list((ROOT/'phone/wifi-setup').glob('*'))+list((ROOT/'test/file_history').glob('*.mjs'))
 return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths) if p.is_file()}
def main():
 OUT.mkdir(parents=True,exist_ok=True);before=hashes();command=['node','--test','--test-reporter=tap','test/file_history/phone_protocol.test.mjs','test/file_history/phone_ui.test.mjs']
 r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=60);log=OUT/'phone.tap';log.write_text(r.stdout+r.stderr)
 totals={k:int(v) for k,v in re.findall(r'^# (tests|pass|fail|cancelled|skipped|todo) (\d+)$',r.stdout,re.M)}
 after=hashes();passed=r.returncode==0 and not r.stderr and totals.get('tests',0)==totals.get('pass',-1) and all(totals.get(k)==0 for k in ['fail','cancelled','skipped','todo']) and before==after
 result=dict(passed=passed,source_stable=before==after,source_sha256=after,command=command,exit=r.returncode,stderr_empty=not r.stderr,totals=totals,log=str(log.relative_to(ROOT)),log_sha256=hashlib.sha256(log.read_bytes()).hexdigest(),physical_radio=False,private_files=False,cloud_contact=False,website_contact=False,cryptographic_interoperability_claim=False)
 (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['passed','totals','stderr_empty','source_stable']},indent=2))
 if not passed:raise RuntimeError('Phone gate failed: '+str(log))
if __name__=='__main__':main()
