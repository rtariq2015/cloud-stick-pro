"""Stage only the five public UI files in the already-opened Sites checkout."""
from pathlib import Path
import hashlib,json

ROOT=Path(__file__).resolve().parents[1]
SITE=ROOT.parent/'cloud-stick-phone-site'
OUT=ROOT/'artifacts/backup-wifi-integration/phone-site'
VERSION='backup-wifi-phone-20261104-r5'
FILES=('index.html','style.css','app.mjs','connection.mjs','security2.mjs')
PROJECT='appgprj_6aea321ca33c81918bee82df128f2062'
assert json.loads((SITE/'.openai/hosting.json').read_text())['project_id']==PROJECT
old=(SITE/'worker/index.js').read_text()
a=old.index('const policy=')+len('const policy=');b=old.index(';\nconst entry=',a)
policy=json.loads(old[a:b])
assert "connect-src 'none'" in policy['Content-Security-Policy']
assert 'bluetooth=(self)' in policy['Permissions-Policy']
assets={};records={}
for name in FILES:
 p=ROOT/'phone/wifi-setup'/name;assert p.is_file() and not p.is_symlink()
 data=p.read_bytes();assert len(data)<512*1024
 mime='text/html; charset=utf-8' if name.endswith('.html') else 'text/css; charset=utf-8' if name.endswith('.css') else 'text/javascript; charset=utf-8'
 digest=hashlib.sha256(data).hexdigest()
 assets[f'/releases/{VERSION}/{name}']={'body':data.decode('utf-8'),'mime':mime,'sha256':digest}
 records[name]={'bytes':len(data),'sha256':digest,'mime':mime}
worker='// Public interface bytes only; no outgoing requests, uploads or persistence.\n'
worker+='const assets='+json.dumps(assets,ensure_ascii=True)+';\n'
worker+='const policy='+json.dumps(policy,ensure_ascii=True)+';\n'
worker+='const entry='+json.dumps(f'/releases/{VERSION}/index.html')+';\n'
worker+='const names='+json.dumps(FILES)+';\n'
worker+='''export default {async fetch(request){
 const headers=new Headers(policy);headers.set('Content-Type','text/plain; charset=utf-8');
 if(!['GET','HEAD'].includes(request.method)){
  headers.set('Allow','GET, HEAD');return new Response('Method not allowed',{status:405,headers});
 }
 const url=new URL(request.url),path=url.pathname;
 if(url.search)return new Response(request.method==='HEAD'?null:'Not found',{status:404,headers});
 if(path==='/'||path==='/index.html'){
  headers.set('Location',entry);return new Response(null,{status:302,headers});
 }
 const legacy=path.match(/^\\/releases\\/(?:wifi-m2-phone-20261104-r[12]|backup-wifi-phone-20261104-r[34])\\/([^/]+)$/);
 if(legacy&&names.includes(legacy[1])){
  headers.set('Location',entry.replace('index.html',legacy[1]));return new Response(null,{status:302,headers});
 }
 const asset=Object.hasOwn(assets,path)?assets[path]:null;
 if(!asset)return new Response(request.method==='HEAD'?null:'Not found',{status:404,headers});
 headers.set('Content-Type',asset.mime);headers.set('ETag','"'+asset.sha256+'"');
 return new Response(request.method==='HEAD'?null:asset.body,{status:200,headers});
}};
'''
(SITE/'worker/index.js').write_text(worker)
manifest={'release':VERSION,'project_id':PROJECT,'files':records,'headers':policy,
          'backward_compatible_wifi_ui':True,'owner_key_or_private_files_included':False,
          'firmware_or_backup_included':False,'deployed':False}
(SITE/'release-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'release-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'release':VERSION,'public_files':list(FILES),'deployed':False}))
