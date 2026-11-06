// Actual app, ManagementConnection, log parser and private-file handler. Only
// DOM/GATT/security transport are synthetic. No real private file is opened.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {fixture as transport,events,DiagnosticLogClient,WifiSetupClient,WifiInputError,PublicWifiClient,until} from './fixture.mjs';
const html=readFileSync(new URL('../../phone/wifi-setup/index.html',import.meta.url),'utf8');
const source=readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,'');
const KEY='PUBLIC_OWNER_KEY_'.padEnd(32,'X');
const CARD={version:2,device:'001122334455',username:'csp-owner',owner_key:KEY,synthetic:false,record_sha256:'0'.repeat(64)};
function fixture(options={}){
 class Element{hidden=false;disabled=false;checked=false;textContent='';value='';type='';children=[];
  append(child){this.children.push(child);}replaceChildren(...children){this.children=children;}
  set innerHTML(_){throw new Error('Unsafe markup rendering');}}
 const e={};for(const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)){
  const element=new Element();element.hidden=/\bhidden\b/.test(match[0]);element.disabled=/\bdisabled\b/.test(match[0]);element.type=match[0].match(/\btype="([^"]+)"/)?.[1]??'';e[match[1]]=element;
 }
 const f=transport(options),windowEvents=new Map(),documentEvents=new Map(),connections=[],keys=[];let reads=0,networkCalls=0;
 const document={visibilityState:'visible',getElementById:id=>e[id],createElement:()=>new Element(),createTextNode:text=>({textContent:text}),addEventListener:(name,fn)=>documentEvents.set(name,fn)};
 const blocked=()=>{++networkCalls;throw new Error('No network/storage access allowed in private import');};
 const scope={ManagementConnection:class{constructor(next){keys.push(next.ownerKey);const c=f.create(next);connections.push(c);return c;}},WifiSetupClient,WifiInputError,PublicWifiClient,DiagnosticLogClient,
  document,location:{protocol:options.protocol??'https:'},isSecureContext:true,navigator:{bluetooth:{requestDevice:blocked},sendBeacon:blocked},TextEncoder,fetch:blocked,
  localStorage:new Proxy({},{get:blocked,set:blocked}),sessionStorage:new Proxy({},{get:blocked,set:blocked}),
  addEventListener:(name,fn)=>windowEvents.set(name,fn)};
 vm.runInNewContext(source+'\nglobalThis.inspect=()=>({flow:!!flow,ownerCardDevice});',scope);
 const event=(name,data={persisted:true})=>(windowEvents.get(name)||documentEvents.get(name))?.(data);
 const text=element=>element.textContent+' '+element.value+' '+element.children.map(text).join(' ');
 return {...f,e,document,keys,get waiting(){return f.waiting;},get reads(){return reads;},get networkCalls(){return networkCalls;},event,
  inspect:()=>scope.inspect(),domText:()=>Object.values(e).map(text).join('\n'),
  file(content,{size=new TextEncoder().encode(content).length,promise}={}){
   e['owner-card'].files=[{size,get name(){throw new Error('Private filename must not be inspected');},async text(){++reads;return promise?promise:content;}}];
  },cleanup:()=>connections.forEach(connection=>connection.disconnect())};
}
async function connect(f){f.e['owner-key'].value=KEY;await f.e.connect.onclick();assert.equal(f.e['connection-state'].textContent,'Private owner authenticated');}
function cleared(f){assert(f.e['activity-card'].hidden);assert.equal(f.e['activity-state'].textContent,'');assert.equal(f.e['activity-storage'].textContent,'');assert.equal(f.e['activity-list'].children.length,0);assert.equal(f.e['owner-key'].value,'');assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');}
test('authenticated activity shows only recorded numeric facts with clock and partial-scan caveats',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);assert(!f.e['activity-card'].hidden);assert.equal(f.e['activity-list'].children.length,3);
 const text=f.e['activity-list'].children.map(e=>e.textContent).join(' ');assert.match(text,/Clock unavailable/);assert.match(text,/network clock/);assert.match(text,/Reported verified files in that scan: 2/);assert.match(text,/may have been partial/);
 assert.match(f.e['activity-state'].textContent,/history, not a live upload/);assert.match(html,/verified backup receipts remain the record/);
 assert(!text.includes('PRIVATE_SYNTHETIC_VENUE'));assert(!text.includes(KEY));assert.equal(f.networkCalls,0);
});
test('empty history has no success or uploaded-file assertion',async t=>{
 const f=fixture({events:[]});t.after(f.cleanup);await connect(f);assert(!f.e['activity-card'].hidden);assert.match(f.e['activity-state'].textContent,/No recent/);assert.equal(f.e['activity-list'].children.length,0);
});
test('all persistence/corruption/unloaded flags produce explicit caveats without changing receipt facts',async t=>{
 const f=fixture({status:{loaded:false,dirty:true,corrupt:true,io_failed:true,exhausted:true,persisted:0}});t.after(f.cleanup);await connect(f);
 const text=f.e['activity-storage'].textContent;for(const phrase of [/not loaded/,/corruption/,/could not be read or saved/,/still in RAM/,/limit was reached/])assert.match(text,phrase);
 assert(!/files (?:uploaded|verified)|backup succeeded/i.test(text));
});
test('every event kind reports typed facts and a connection check never claims an upload',async t=>{
 const rows=Array.from({length:8},(_,i)=>({s:i+1,u:i,t:0,k:i+1,c:0}));const f=fixture({events:rows});t.after(f.cleanup);await connect(f);
 const text=f.e['activity-list'].children.map(e=>e.textContent).join(' ');assert.match(text,/Scan finished/);assert.match(text,/File verification must be checked/);assert.match(text,/This does not prove that any file uploaded/);assert.match(text,/Reported verified files/);
});
for(const capability of [null,false])test('no activity query or card for default capability '+capability,async t=>{
 const f=fixture({capability});t.after(f.cleanup);await connect(f);assert(f.e['activity-card'].hidden);assert(!f.requests.some(r=>r.op==='event_log'));assert(!f.e['wifi-card'].hidden);
});
for(const reason of ['disconnect','remote-disconnect','pagehide','pageshow','freeze','resume','visibilitychange'])test(reason+' scrubs authenticated activity and private key input',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);assert(!f.e['activity-card'].hidden);
 if(reason==='disconnect')f.e.disconnect.onclick();else if(reason==='remote-disconnect')f.remoteDisconnect();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}
 cleared(f);f.document.visibilityState='visible';f.event('pageshow');cleared(f);
});
for(const cursor of [0,21])for(const reason of ['disconnect','pagehide','freeze','visibilitychange'])test('late log page cursor '+cursor+' after '+reason+' cannot restore private facts',async t=>{
 const f=fixture({delayCursor:cursor});t.after(f.cleanup);f.e['owner-key'].value=KEY;const pending=f.e.connect.onclick();await until(()=>f.waiting);
 if(reason==='disconnect')f.e.disconnect.onclick();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}
 f.release();await pending;cleared(f);assert.equal(f.requests.filter(r=>r.op==='event_log').length,cursor===0?1:2);
});
test('malformed private activity closes setup without reflecting injected filename text',async t=>{
 const f=fixture({page:page=>({...page,filename:'PRIVATE_MUST_NOT_REFLECT.pdf'})});t.after(f.cleanup);f.e['owner-key'].value=KEY;await f.e.connect.onclick();cleared(f);assert(!f.domText().includes('PRIVATE_MUST_NOT_REFLECT'));
});
test('public Wi-Fi handoff retains only public AP instructions and clears activity',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);await f.e['portal-prepare'].onclick();await f.e['portal-continue'].onclick();cleared(f);assert(!f.e['portal-handoff-card'].hidden);assert.equal(f.e['portal-ap-name'].textContent,'CSP-Guest-a1b2c3');
});
test('TXT key loads locally, trims whitespace, never reads a filename and clears on actual connect',async t=>{
 const f=fixture();t.after(f.cleanup);f.file(' \r\n'+KEY+'\n');await f.e['owner-card'].onchange();assert.equal(f.e['owner-key'].value,KEY);assert.equal(f.inspect().ownerCardDevice,null);assert.equal(f.e['owner-card'].value,'');assert.match(f.e['owner-card-message'].textContent,/loaded locally/);
 await f.e.connect.onclick();assert.equal(f.keys[0],KEY);assert.equal(f.e['owner-key'].value,'');assert(!f.e['activity-card'].hidden);assert.equal(f.networkCalls,0);
 assert(!f.requests.some(request=>JSON.stringify(request).includes(KEY)));
});
test('TXT key clears a previously imported JSON card identity binding',async t=>{
 const f=fixture();t.after(f.cleanup);f.file(JSON.stringify({...CARD,device:'ffffffffffff'}));await f.e['owner-card'].onchange();assert.equal(f.inspect().ownerCardDevice,'ffffffffffff');
 f.file(KEY);await f.e['owner-card'].onchange();assert.equal(f.inspect().ownerCardDevice,null);await f.e.connect.onclick();assert(!f.e['wifi-card'].hidden);
});
test('strict JSON card identity binding still prevents a different device reading Wi-Fi/activity',async t=>{
 const f=fixture();t.after(f.cleanup);f.file(JSON.stringify({...CARD,device:'ffffffffffff'}));await f.e['owner-card'].onchange();await f.e.connect.onclick();cleared(f);assert.equal(f.requests.length,0);assert.match(f.e['connection-message'].textContent,/different Cloud Stick/);
});
test('valid strict JSON card still binds the exact authenticated device',async t=>{
 const f=fixture();t.after(f.cleanup);f.file(JSON.stringify(CARD));await f.e['owner-card'].onchange();assert.equal(f.inspect().ownerCardDevice,CARD.device);await f.e.connect.onclick();assert(!f.e['activity-card'].hidden);assert.equal(f.networkCalls,0);
});
for(const value of [KEY.slice(1),KEY+'X',KEY.slice(0,10)+' '+KEY.slice(11),KEY.slice(0,10)+'/'+KEY.slice(11),KEY.slice(0,10)+'\0'+KEY.slice(11),KEY.slice(0,10)+'\n'+KEY.slice(11),'😀'.repeat(32),'PRIVATE_MUST_NOT_REFLECT'])test('invalid TXT key '+JSON.stringify(value.length)+' bytes/content is rejected without reflection',async t=>{
 const f=fixture();t.after(f.cleanup);f.file(value);await f.e['owner-card'].onchange();assert.equal(f.e['owner-key'].value,'');assert.equal(f.keys.length,0);assert.match(f.e['owner-card-message'].textContent,/not a supported/);assert(!f.e['owner-card-message'].textContent.includes('PRIVATE_MUST_NOT_REFLECT'));assert.equal(f.networkCalls,0);
});
test('TXT whitespace is bounded to 256 UTF-8 bytes even when trimming yields a key',async t=>{
 const f=fixture();t.after(f.cleanup);f.file(' '.repeat(225)+KEY);await f.e['owner-card'].onchange();assert.equal(f.e['owner-key'].value,'');assert.match(f.e['owner-card-message'].textContent,/not a supported/);
});
test('oversized private file is rejected before text is read',async t=>{
 const f=fixture();t.after(f.cleanup);f.file(KEY,{size:2049});await f.e['owner-card'].onchange();assert.equal(f.reads,0);assert.equal(f.e['owner-key'].value,'');
});
test('misreported small file size cannot bypass the actual UTF-8 byte cap',async t=>{
 const f=fixture();t.after(f.cleanup);f.file(' '.repeat(2048)+KEY,{size:32});await f.e['owner-card'].onchange();assert.equal(f.reads,1);assert.equal(f.e['owner-key'].value,'');
});
for(const reason of ['pagehide','pageshow','freeze','resume','visibilitychange'])test('late TXT read after '+reason+' cannot restore a key',async t=>{
 const f=fixture();t.after(f.cleanup);let release;const promise=new Promise(resolve=>release=resolve);f.file(KEY,{promise});const pending=f.e['owner-card'].onchange();
 if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);release(KEY);await pending;assert.equal(f.e['owner-key'].value,'');assert.equal(f.inspect().ownerCardDevice,null);assert.equal(f.keys.length,0);assert.equal(f.networkCalls,0);
});
test('HTTP never reads a private TXT file',async t=>{
 const f=fixture({protocol:'http:'});t.after(f.cleanup);f.file(KEY);await f.e['owner-card'].onchange();assert.equal(f.reads,0);assert.equal(f.e['owner-key'].value,'');assert(f.e['owner-card'].disabled);
});
test('an authenticated session refuses a newly selected key file',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);f.file(KEY);await f.e['owner-card'].onchange();assert.equal(f.reads,0);assert.equal(f.e['owner-key'].value,'');assert(f.e['owner-card'].disabled);
});
