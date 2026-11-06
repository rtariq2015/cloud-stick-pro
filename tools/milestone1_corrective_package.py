"""Package explicit public review materials. No device/service/publish operations."""
import argparse,hashlib,io,json,subprocess,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/milestone1-corrections'
def sha(data):return hashlib.sha256(data).hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args])
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--source-commit',required=True);args=parser.parse_args()
    source=git('rev-parse',args.source_commit+'^{commit}').decode().strip()
    revision=git('rev-parse','HEAD').decode().strip()
    assert not git('status','--porcelain'),'Commit all reviewed source/docs before packaging'
    comparison=json.loads((OUT/'BUILD_COMPARISON.json').read_text())
    for label in ['disabled','isolated']:
        expected=comparison['builds'][label]['firmware_sha256']
        assert sha((OUT/label/'firmware.bin').read_bytes())==expected
    assert comparison['partition_binaries_unchanged']
    for name in ['adapter/results.json','isolation/results.json','sdk-lifecycle/result.json',
                 'raw-flash-guard/result.json','sdk-link/tdongle_s3_ble_m1_isolated/result.json',
                 'sdk-link/tdongle_s3/result.json']:
        result=json.loads((OUT/name).read_text())
        assert result.get('passed',result.get('pass')),name
    dependencies=json.loads((ROOT/'artifacts/milestone1/baseline/baseline_manifest.json').read_text())['dependencies']
    evidence={}
    for path in sorted(OUT.rglob('*')):
        if not path.is_file() or path.is_symlink():continue
        relative=path.relative_to(OUT)
        if relative.parts[0] in ['phone-https-package','phone-https-package-final']:continue
        if path.name in ['REVIEW_MANIFEST.json','SHA256SUMS']:continue
        if path.suffix in ['.json','.log','.txt','.png','.inc']:
            evidence[str(relative)]={'bytes':path.stat().st_size,'sha256':sha(path.read_bytes())}
    package=Path('/config/.unmanic/releases')/('cloud-stick-pro-m1-corrective-'+source[:7]+'.zip')
    manifest={'schema':1,'source_commit':source,'review_documentation_commit':revision,
        'branch':git('branch','--show-current').decode().strip(),
        'reviewed_prior_source':'d541c13cfddd400486409ce375dc0215f7e693cd',
        'builds':comparison['builds'],'dependencies':dependencies,
        'R1_R8_source_desktop_verdict':'PASS','physical_hardware_readiness':'FAIL - approval/fresh backup/physical gates open',
        'device_contact':False,'public_deployment':False,'real_cloud_access':False,
        'phone_release':'m1-corrections-20261103-r2','evidence':evidence,
        'preserved_recovery_app_sha256':sha((ROOT/'artifacts/0.3.3/firmware.bin').read_bytes()),
        'fresh_live_backup':False,'package':str(package)}
    raw=(json.dumps(manifest,indent=2)+'\n').encode();(OUT/'REVIEW_MANIFEST.json').write_bytes(raw)
    with zipfile.ZipFile(package,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        archive.writestr('source.zip',git('archive','--format=zip',revision))
        archive.writestr('REVIEW_MANIFEST.json',raw)
        for label in ['disabled','isolated']:
            for name in ['firmware.bin','firmware.elf','firmware.map','partitions.bin','build.json','sections.json','symbols.txt']:
                archive.write(OUT/label/name,label+'/'+name)
        for relative in evidence:
            if relative.split('/')[0] in ['disabled','isolated']:continue
            archive.write(OUT/relative,'evidence/'+relative)
        web=OUT/'phone-https-package-final'
        for path in sorted(web.rglob('*')):
            if path.is_file():
                assert not path.is_symlink()
                archive.write(path,'phone-https-package/'+str(path.relative_to(web)))
        for name in ['firmware.bin','partitions.bin']:
            archive.write(ROOT/'artifacts/0.3.3'/name,'preserved-0.3.3/'+name)
        for name in ['MILESTONE1_CORRECTIVE_CHANGES.md','MILESTONE1_TEST_RESULTS.md','BLE_MEMORY_AND_LIFECYCLE.md','PHONE_HTTPS_DEPLOYMENT_PACKAGE.md','UPDATED_HARDWARE_READINESS.md']:
            archive.write(ROOT/name,name)
    with zipfile.ZipFile(package) as archive:assert archive.testzip() is None
    receipt={'file':str(package),'bytes':package.stat().st_size,'sha256':sha(package.read_bytes()),'source_commit':source,'review_documentation_commit':revision,'crc_verified':True}
    package.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
