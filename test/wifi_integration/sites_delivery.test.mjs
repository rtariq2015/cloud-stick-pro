// Exact generated Worker fetch behavior; no deployed origin or browser involved.
import {readFileSync,readdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const root=resolve(process.argv[2]);
const manifest=JSON.parse(readFileSync(root+'/release-manifest.json','utf8'));
const source=readFileSync(root+'/dist/server/index.js','utf8');
const {default:app}=await import('data:text/javascript;base64,'+Buffer.from(source).toString('base64'));
const names=['index.html','style.css','app.mjs','connection.mjs','security2.mjs'];
assert.deepEqual(readdirSync(root+'/runtime').sort(),names.toSorted());
assert.deepEqual(Object.keys(manifest.files).sort(),names.toSorted());
let checks=0;
function policies(response){for(const [key,value] of Object.entries(manifest.headers)){assert.equal(response.headers.get(key),value);checks++;}}
for(const name of names){
 const raw=readFileSync(root+'/runtime/'+name),expected=manifest.files[name];
 assert.equal(raw.length,expected.bytes);assert.equal(createHash('sha256').update(raw).digest('hex'),expected.sha256);checks+=2;
 for(const method of ['GET','HEAD']){
  const response=await app.fetch(new Request('https://test.invalid'+manifest.entry_path.replace('index.html',name),{method}));
  assert.equal(response.status,200);assert.equal(response.headers.get('Content-Type'),expected.mime);policies(response);checks+=2;
  const body=Buffer.from(await response.arrayBuffer());
  if(method==='HEAD')assert.equal(body.length,0);else assert.equal(createHash('sha256').update(body).digest('hex'),expected.sha256);
  checks++;
 }
}
for(const path of ['/','/index.html']){
 const r=await app.fetch(new Request('https://test.invalid'+path));assert.equal(r.status,302);assert.equal(r.headers.get('Location'),manifest.entry_path);policies(r);checks+=2;
}
const prefix=manifest.entry_path.slice(0,-'index.html'.length);
for(const path of ['/.git/config','/owner-card.json','/owner-record.bin','/firmware.bin','/release-manifest.json','/worker/index.js','/runtime/app.mjs','/_headers','/app.mjs','/constructor','/__proto__',prefix+'owner-card.json',prefix+'index.html?file=owner-card.json',prefix+'%69ndex.html']){
 const r=await app.fetch(new Request('https://test.invalid'+path));assert.equal(r.status,404,path);policies(r);checks++;
}
for(const method of ['POST','PUT','DELETE','PATCH','OPTIONS']){
 const r=await app.fetch(new Request('https://test.invalid'+manifest.entry_path,{method}));assert.equal(r.status,405);assert.equal(r.headers.get('Allow'),'GET, HEAD');policies(r);checks+=2;
}
assert(!/\bfetch\s*\(/.test(source.replace('async fetch(request)','async handler(request)')),'No outgoing fetch permitted');
console.log(JSON.stringify({passed:true,checks,files:5,excluded_paths:14,write_methods_denied:5,exact_bytes:true,desktop_worker_only:true,tls_or_android_test:false}));
