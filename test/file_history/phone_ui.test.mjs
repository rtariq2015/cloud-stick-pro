// Actual app/ManagementConnection/history parser; simulated DOM and GATT only.
import {test} from 'node:test';import assert from 'node:assert/strict';import {readFileSync} from 'node:fs';import vm from 'node:vm';
import {fixture as transport,rows,FileHistoryClient,WifiSetupClient,WifiInputError,PublicWifiClient,DiagnosticLogClient,until} from './fixture.mjs';
const html=readFileSync(new URL('../../phone/wifi-setup/index.html',import.meta.url),'utf8');
const source=readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,'');
const KEY='PUBLIC_OWNER_KEY_'.padEnd(32,'X');
function fixture(options={}){
 class Element{hidden=false;disabled=false;checked=false;textContent='';value='';type='';children=[];append(...children){this.children.push(...children);}replaceChildren(...c){this.children=c;}set innerHTML(_){throw new Error('Markup injection');}}
 const e={};for(const m of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)){const x=new Element;x.hidden=/\bhidden\b/.test(m[0]);x.disabled=/\bdisabled\b/.test(m[0]);e[m[1]]=x;}
 const f=transport(options),events=new Map(),connections=[];let networkCalls=0;
 const document={visibilityState:'visible',getElementById:id=>e[id],createElement:()=>new Element,createTextNode:t=>({textContent:t}),addEventListener:(k,v)=>events.set(k,v)};
 const blocked=()=>{++networkCalls;throw new Error('No network or private storage allowed');};
 const scope={ManagementConnection:class{constructor(o){const c=f.create(o);connections.push(c);return c;}},WifiSetupClient,WifiInputError,PublicWifiClient,DiagnosticLogClient,FileHistoryClient,document,location:{protocol:'https:'},isSecureContext:true,navigator:{bluetooth:{requestDevice:blocked}},TextEncoder,fetch:blocked,addEventListener:(k,v)=>events.set(k,v)};
 vm.runInNewContext(source,scope);const text=x=>x.textContent+' '+x.children.map(text).join(' ');
 return {...f,e,document,get waiting(){return f.waiting;},get networkCalls(){return networkCalls;},event:k=>events.get(k)?.({persisted:true}),text:()=>['file-history-state','file-history-warning','file-history-list'].map(id=>text(e[id])).join(' '),cleanup:()=>connections.forEach(c=>c.disconnect())};
}
async function connect(f){f.e['owner-key'].value=KEY;await f.e.connect.onclick();assert.equal(f.e['connection-state'].textContent,'Private owner authenticated');}
function clear(f){assert(f.e['file-history-card'].hidden);assert.equal(f.e['file-history-state'].textContent,'');assert.equal(f.e['file-history-warning'].textContent,'');assert.equal(f.e['file-history-list'].children.length,0);assert(!f.e['connection-state'].textContent.includes('Private owner authenticated'));assert.equal(f.e['owner-key'].value,'');}
test('file-by-file saved facts render as text only and preserve waiting/verification distinction',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);assert(!f.e['file-history-card'].hidden);assert.equal(f.e['file-history-list'].children.length,3);
 const text=f.text();assert(text.includes(rows[0].path));assert.match(text,/Cloud copy verified/);assert.match(text,/Uploaded; content verification pending/);assert.match(text,/Retry scheduled/);assert.match(text,/2 MiB/);assert.match(text,/18446744073709551615 bytes/);assert(!text.includes(KEY));assert.equal(f.networkCalls,0);
 assert.match(html,/up to eight waiting or recently verified records/);assert.match(html,/later scan/);assert.match(html,/Backups are paused/);
});
for(const capability of [null,false])test('current device with no history capability '+capability+' keeps the optional card hidden',async t=>{const f=fixture({capability});t.after(f.cleanup);await connect(f);assert(f.e['file-history-card'].hidden);assert(!f.requests.some(r=>r.op.startsWith('history_')));});
test('incomplete, corrupt, I/O and limited states have honest caveats',async t=>{const f=fixture({status:{complete:false,limited:true,corrupt:true,io_failed:true}});t.after(f.cleanup);await connect(f);const text=f.text();for(const p of [/up to eight/,/listing was incomplete/,/damaged saved record/,/could not be read/])assert.match(text,p);});
for(const reason of ['disconnect','remote-disconnect','pagehide','pageshow','freeze','resume','visibilitychange'])test(reason+' erases all authenticated filenames/results',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);if(reason==='disconnect')f.e.disconnect.onclick();else if(reason==='remote-disconnect')f.remoteDisconnect();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}clear(f);f.event('pageshow');clear(f);
});
for(const reason of ['disconnect','pagehide','freeze','visibilitychange'])for(const delayStatus of [true,false])test('late history '+(delayStatus?'status':'path')+' after '+reason+' cannot repaint private rows',async t=>{
 const f=fixture({delayStatus,delayOffset:delayStatus?undefined:64,rows:[rows[1]]});t.after(f.cleanup);f.e['owner-key'].value=KEY;const pending=f.e.connect.onclick();await until(()=>f.waiting);
 if(reason==='disconnect')f.e.disconnect.onclick();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}f.release();await pending;clear(f);
});
test('invalid authenticated fragment closes UI without reflecting injected content',async t=>{const f=fixture({fragment:r=>({...r,private:'PRIVATE_NO_REFLECT'})});t.after(f.cleanup);f.e['owner-key'].value=KEY;await f.e.connect.onclick();clear(f);assert(!f.text().includes('PRIVATE_NO_REFLECT'));});
