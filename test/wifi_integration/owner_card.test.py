"""Synthetic-only offline card preparation, permissions, no secret stdout."""
from pathlib import Path
from tempfile import TemporaryDirectory
import hashlib
import json
import os
import stat
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
tool=root/'tools/create_wifi_owner_card.py'
out=root/'artifacts/wifi-integration'
results=[]
with TemporaryDirectory() as temporary:
    directory=Path(temporary);os.chmod(directory,0o700)
    command=[sys.executable,str(tool),'--device','001122334455','--private-directory',str(directory),'--synthetic']
    r=subprocess.run(command,capture_output=True,text=True,timeout=15)
    assert r.returncode==0 and not r.stderr
    public=json.loads(r.stdout);card=json.loads((directory/'owner-card.json').read_text())
    assert card['synthetic'] and card['owner_key'] not in r.stdout and card['owner_key'] not in r.stderr
    for name in ['owner-card.json','owner-record.bin']:
        assert stat.S_IMODE((directory/name).stat().st_mode)==0o600
    before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.iterdir()}
    r=subprocess.run(command,capture_output=True,text=True,timeout=15)
    assert r.returncode!=0 and card['owner_key'] not in r.stderr+r.stdout
    after={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.iterdir()}
    assert before==after
    results.append({'case':'synthetic-only creation, no secret stdout, immutable pair, 0600 files','passed':True})
with TemporaryDirectory() as temporary:
    os.chmod(temporary,0o755)
    r=subprocess.run([sys.executable,str(tool),'--device','001122334455','--private-directory',temporary,'--synthetic'],capture_output=True,text=True)
    assert r.returncode!=0 and not list(Path(temporary).iterdir())
    results.append({'case':'unsafe directory rejected without output files','passed':True})
with TemporaryDirectory() as temporary:
    base=Path(temporary);private=base/'private';private.mkdir(mode=0o700);link=base/'link';link.symlink_to(private,target_is_directory=True)
    r=subprocess.run([sys.executable,str(tool),'--device','001122334455','--private-directory',str(link),'--synthetic'],capture_output=True,text=True)
    assert r.returncode!=0 and not list(private.iterdir())
    results.append({'case':'symlink directory rejected','passed':True})
(out/'owner-card-results.json').write_text(json.dumps({'passed':True,'real_keys_generated':False,'results':results},indent=2)+'\n')
print('PASS synthetic owner-card creation, file protection, no secret stdout, no overwrite, unsafe/symlink denial')
