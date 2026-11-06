"""Package the reviewed source/test artifacts only. No device or network access."""
from pathlib import Path
import hashlib,json,subprocess,zipfile
root=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
assert not git('status','--porcelain').strip(),'Commit the reviewed source before packaging'
commit=git('rev-parse','HEAD').decode().strip()
evidence=root/'artifacts/milestone1'
comparison=json.loads((evidence/'BUILD_COMPARISON.json').read_text())
expected='69692828fd3a8c2a09f23b62d43df0d5ad840a64f386ef87d92b92f18436e302'
assert comparison['builds']['enabled']['firmware_sha256']==expected
assert hashlib.sha256((evidence/'enabled/firmware.bin').read_bytes()).hexdigest()==expected
assert comparison['partition_binaries_identical']
assert all(comparison['protected_sources_unchanged'].values())
tracked=[root/name.decode() for name in git('ls-files','-z').split(b'\0') if name]
sources={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest()
 for p in tracked if p.is_file() and (str(p.relative_to(root)).startswith(('src/','include/','boards/','lib/','phone/milestone1/')) or p.name=='platformio.ini')}
manifest={'source_commit':commit,'baseline_commit':'468b66331db687fec26565d8373cd01cbb534ee1',
 'branch':git('branch','--show-current').decode().strip(),'status':'SYNTHETIC SOURCE PROTOTYPE - NOT READY TO FLASH',
 'hardware_flashed':False,'current_device_flash_hash_verified':False,
 'enabled_image_sha256':expected,'disabled_image_sha256':comparison['builds']['disabled']['firmware_sha256'],
 'partition_binary_sha256':comparison['builds']['enabled']['partitions_binary_sha256'],
 'runtime_source_sha256':sources,'private_flash_or_nvs_included':False}
(evidence/'REVIEW_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
skip={'ble-policy-tests','guest-dns-tests','security2-host','security2-host-asan'}
checks=[]
for p in sorted(evidence.rglob('*')):
 if p.is_file() and p.name!='SHA256SUMS' and p.name not in skip:checks.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(evidence)))
(evidence/'SHA256SUMS').write_text('\n'.join(checks)+'\n')
release=root.parents[1]/'releases';release.mkdir(exist_ok=True)
stem='cloud-stick-pro-ble-m1-review-'+commit[:7]
archive=release/(stem+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p in tracked:
  assert p.is_file() and not p.is_symlink(),p
  z.write(p,'cloud-stick-pro/'+str(p.relative_to(root)))
 for p in sorted(evidence.rglob('*')):
  if p.is_file() and p.name not in skip:z.write(p,'cloud-stick-pro/'+str(p.relative_to(root)))
 for name in ['firmware.bin','partitions.bin','HARDWARE_RESULTS.json']:
  p=root/'artifacts/0.3.3'/name
  z.write(p,'preserved-0.3.3/'+name)
 z.writestr('READ-FIRST.txt','SYNTHETIC SOURCE PROTOTYPE - NOT READY TO FLASH\n'
  'No hardware was changed. Read cloud-stick-pro/DEVICE_FLASH_READINESS_CHECKLIST.md.\n'
  'Private NVS/full-flash backups are excluded. No auto-flash script is supplied.\n'
  'preserved-0.3.3 is historical release material, not a fresh live configuration backup.\n')
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
sha=hashlib.sha256(archive.read_bytes()).hexdigest()
(release/(stem+'.sha256')).write_text(sha+'  '+archive.name+'\n')
print(json.dumps({'archive':str(archive),'bytes':archive.stat().st_size,'sha256':sha,'source_commit':commit,'zip_integrity':'passed','hardware_actions':False}))
