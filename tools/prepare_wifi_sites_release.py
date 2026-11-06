"""Prepare an allowlisted Sites Worker release locally. No network or publishing.

Only five embedded runtime files are delivered; source, manifests and firmware
are never HTTP routes. Pass the existing native-returned project ID unchanged.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
from prepare_wifi_phone_package import FILES, MIMES, HEADERS

ROOT=Path(__file__).resolve().parents[1]

def prepare(output,release,project_id):
    assert re.fullmatch(r'[a-z0-9][a-z0-9.-]{0,79}',release)
    output=Path(output).absolute()
    assert output.resolve()==output and ROOT/'artifacts' in output.parents
    site=output/'site';site.mkdir(parents=True,exist_ok=False)
    for folder in ['runtime','worker','scripts','.openai']:(site/folder).mkdir()
    prefix='/releases/'+release+'/'
    headers=dict(HEADERS)
    headers['Cache-Control']='no-store, no-transform'
    headers['Content-Security-Policy']+="; frame-src 'none'; worker-src 'none'; manifest-src 'none'"
    headers['Permissions-Policy']+=', usb=(), serial=()'
    assets={};files={}
    for name in FILES:
        source=ROOT/'phone/wifi-setup'/name
        assert source.is_file() and not source.is_symlink()
        raw=source.read_bytes();shutil.copyfile(source,site/'runtime'/name)
        files[name]={'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'mime':MIMES[name]}
        assets[prefix+name]={'body':raw.decode('utf-8'),'mime':MIMES[name]}
    manifest={'release':release,'protocol':'csp-wifi v2 / Espressif Security2 patch1',
              'entry_path':prefix+'index.html','files':files,'headers':headers,
              'public_file_count':5,'local_only':True,'deployed':False,
              'private_card_or_firmware_included':False}
    worker='// Exact-byte delivery only. No outgoing network, persistence or uploads.\n'
    worker+='const assets='+json.dumps(assets,ensure_ascii=False)+';\n'
    worker+='const policy='+json.dumps(headers)+';\n'
    worker+='const entry='+json.dumps(manifest['entry_path'])+';\n'
    worker+='''export default {async fetch(request){
 const headers=new Headers(policy);
 headers.set('Content-Type','text/plain; charset=utf-8');
 if(!['GET','HEAD'].includes(request.method)){
  headers.set('Allow','GET, HEAD');return new Response('Method not allowed',{status:405,headers});
 }
 const url=new URL(request.url),path=url.pathname;
 if(url.search)return new Response(request.method==='HEAD'?null:'Not found',{status:404,headers});
 if(path==='/'||path==='/index.html'){
  headers.set('Location',entry);return new Response(null,{status:302,headers});
 }
 const asset=Object.hasOwn(assets,path)?assets[path]:null;
 if(!asset)return new Response(request.method==='HEAD'?null:'Not found',{status:404,headers});
 headers.set('Content-Type',asset.mime);
 return new Response(request.method==='HEAD'?null:asset.body,{status:200,headers});
}};
'''
    (site/'worker/index.js').write_text(worker)
    (site/'release-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (site/'package.json').write_text(json.dumps({'name':'cloud-stick-private-wifi-phone','private':True,'type':'module'},indent=2)+'\n')
    (site/'.openai/hosting.json').write_text(json.dumps({'project_id':project_id},indent=2)+'\n')
    build='''#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$project_root/dist/server" "$project_root/dist/.openai"
cp "$project_root/worker/index.js" "$project_root/dist/server/index.js"
cp "$project_root/.openai/hosting.json" "$project_root/dist/.openai/hosting.json"
'''
    (site/'scripts/build.sh').write_text(build)
    return site

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--release',required=True)
    parser.add_argument('--project-id',required=True)
    args=parser.parse_args()
    print('Local-only five-file Site candidate:',prepare(args.output,args.release,args.project_id))
