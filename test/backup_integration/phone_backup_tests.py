"""Record source-only UI/parser tests; never publish or connect Bluetooth."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/backup-wifi-integration/phone-backup'
OUT.mkdir(parents=True,exist_ok=True)
commands=[('app-syntax',['node','--check','phone/wifi-setup/app.mjs']),
          ('ui-and-protocol',['node','--test','--test-reporter=tap',
           'test/backup_integration/phone_backup_ui.test.mjs',
           'test/backup_integration/phone_backup_admission.test.mjs',
           'test/wifi_integration/phone-ui.test.mjs'])]
records=[]
for name,command in commands:
    run=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=60)
    (OUT/(name+'.log')).write_text(run.stdout+run.stderr)
    passed=run.returncode==0 and not run.stderr
    records.append({'name':name,'command':command,'exit':run.returncode,
                    'stderr_empty':not run.stderr,'passed':passed})
    print(name,'PASS' if passed else 'FAIL',flush=True)
    if not passed:print(run.stdout+run.stderr);break
files=['phone/wifi-setup/index.html','phone/wifi-setup/app.mjs','phone/wifi-setup/style.css',
       'phone/wifi-setup/connection.mjs','test/backup_integration/phone_backup_ui.test.mjs',
       'test/backup_integration/phone_backup_admission.test.mjs',
       'test/backup_integration/phone_backup_tests.py']
passed=len(records)==len(commands) and all(row['passed'] for row in records)
(OUT/'results.json').write_text(json.dumps({'passed':passed,'desktop_only':True,
    'actual_app_and_status_parser':True,'simulated_DOM_Bluetooth_device':True,
    'physical_device_access':False,'real_cloud_access':False,'published':False,
    'sources':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files},
    'records':records},indent=2)+'\n')
raise SystemExit(0 if passed else 1)
