import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import {DesktopBridge,NativeBluetooth,SERVICE} from '../frontend/bridge.mjs';
const UUID='021aff51-0382-4aea-bff4-6b3f1c5adfb4';
const options={filters:[{namePrefix:'CSP-W2-'}],optionalServices:[SERVICE]};
function signal(){const callbacks=new Set();return {connect:f=>callbacks.add(f),disconnect:f=>callbacks.delete(f),emit:v=>{for(const f of callbacks)f(v);},get size(){return callbacks.size;}};}
function native(){const requests=[],response=signal(),events=signal();return {requests,response,events,request(raw){requests.push(JSON.parse(raw));},answer(result={},ok=true,request=requests.at(-1)){response.emit(JSON.stringify(ok?{id:request.id,ok,result}:{id:request.id,ok,error:{code:result}}));}};}
const tick=()=>new Promise(resolve=>setImmediate(resolve));

test('RPC sends only reviewed operation/argument shapes',async()=>{
 const n=native(),b=new DesktopBridge(n),p=b.request('usb_status',{port:'synthetic-port'});
 assert.deepEqual(n.requests[0],{id:1,op:'usb_status',port:'synthetic-port'});n.answer({firmware:'synthetic'});assert.deepEqual(await p,{firmware:'synthetic'});
 for(const [op,args]of [['exec',{}],['connect',{device_id:'x',password:'synthetic'}],['read',{uuid:'bad'}],['write',{uuid:UUID,hex:'aa'.repeat(513)}],['review_bundle',{path:'/tmp/not-allowed'}]])await assert.rejects(b.request(op,args));
 assert.equal(n.requests.length,1);b.close();
});
test('timer functions are not invoked with the bridge as their receiver',async()=>{
 const n=native();let setCalls=0,clearCalls=0;
 const b=new DesktopBridge(n,{setTimer:function(callback,ms){assert.equal(this,undefined);++setCalls;return setTimeout(callback,ms);},clearTimer:function(handle){assert.equal(this,undefined);++clearCalls;clearTimeout(handle);}});
 const p=b.request('usb_ports');n.answer([]);assert.deepEqual(await p,[]);assert.equal(setCalls,1);assert.equal(clearCalls,1);b.close();
});
test('malformed replies cannot fulfill pending work; exact id is required',async()=>{
 const n=native(),b=new DesktopBridge(n),p=b.request('usb_ports');
 for(const raw of ['not JSON',JSON.stringify({id:999,ok:true,result:[]}),JSON.stringify({id:1,ok:'true',result:[]})])n.response.emit(raw);
 assert.equal(b.pendingCount,1);n.answer([]);assert.deepEqual(await p,[]);b.close();
});
test('ambiguous response fails closed; native exception text never reaches caller',async()=>{
 const n=native(),b=new DesktopBridge(n);const p=b.request('usb_ports');n.response.emit(JSON.stringify({id:1,ok:true,result:[],error:{code:'anything'}}));await assert.rejects(p,e=>e.code==='invalid_response');
 const q=b.request('usb_ports');n.response.emit(JSON.stringify({id:2,ok:false,error:{code:'invalid',message:'synthetic secret'}}));await assert.rejects(q,e=>!e.message.includes('secret'));b.close();
});
test('timeout clears pending job and late response cannot affect next request',async()=>{
 const n=native();let timeout;const b=new DesktopBridge(n,{setTimer:f=>(timeout=f,1),clearTimer:()=>{}});
 const p=b.request('usb_ports');timeout();await assert.rejects(p,e=>e.code==='timeout');assert.equal(b.pendingCount,0);
 const q=b.request('usb_ports');n.answer(['late'],true,n.requests[0]);assert.equal(b.pendingCount,1);n.answer([]);assert.deepEqual(await q,[]);b.close();
});
test('disconnect event rejects GATT work without aborting independent USB request',async()=>{
 const n=native(),b=new DesktopBridge(n);let events=0;b.onDisconnect(()=>++events);
 const p=b.request('read',{uuid:UUID}),q=b.request('usb_ports');
 n.events.emit(JSON.stringify({type:'disconnected',device_id:'synthetic-device'}));await assert.rejects(p,e=>e.code==='disconnected');assert.equal(events,1);n.answer([],true,n.requests[1]);await q;b.close();
});
test('close rejects pending requests, removes subscriptions, and blocks reuse',async()=>{
 const n=native(),b=new DesktopBridge(n),p=b.request('usb_ports');b.close();await assert.rejects(p);assert.equal(n.response.size,0);assert.equal(n.events.size,0);await assert.rejects(b.request('usb_ports'));
});
test('discovery requires explicit selection and filters unrelated named devices',async()=>{
 const n=native(),b=new DesktopBridge(n);let rows;const a=new NativeBluetooth(b,{chooseDevice:async value=>(rows=value,null)});
 const p=a.requestDevice(options);n.answer({devices:[{id:'unrelated',name:'Other'},{id:'one',name:'CSP-W2-synthetic'}]});await assert.rejects(p,e=>e.name==='NotFoundError');
 assert.deepEqual(rows,[{id:'one',name:'CSP-W2-synthetic'}]);assert.equal(n.requests.length,1);assert.equal(a.active,false);a.destroy();b.close();
});
test('invalid and duplicate discovery rows never reach chooser',async()=>{
 for(const devices of [[{id:'x',name:'<unsafe>\n'}],[{id:'x',name:'CSP-W2-a'},{id:'x',name:'CSP-W2-b'}]]){
  const n=native(),b=new DesktopBridge(n);let chosen=false;const a=new NativeBluetooth(b,{chooseDevice:async()=>{chosen=true;return 'x';}});
  const p=a.requestDevice(options);n.answer({devices});await assert.rejects(p);assert.equal(chosen,false);a.destroy();b.close();
 }
});
test('native GATT preserves response-write/read behavior and exact characteristic allowlist',async()=>{
 const n=native(),b=new DesktopBridge(n),a=new NativeBluetooth(b,{chooseDevice:async rows=>rows[0].id});
 const selecting=a.requestDevice(options);n.answer({devices:[{id:'one',name:'CSP-W2-synthetic'}]});const d=await selecting;
 const connecting=d.gatt.connect();n.answer();assert.equal(await connecting,d.gatt);const service=await d.gatt.getPrimaryService(SERVICE),char=await service.getCharacteristic(UUID);
 const writing=char.writeValueWithResponse(new Uint8Array([0,255,3]));assert.deepEqual(n.requests.at(-1),{id:3,op:'write',uuid:UUID,hex:'00ff03'});n.answer();await writing;
 const reading=char.readValue();n.answer({hex:'0102ff'});const data=await reading;assert.deepEqual([...new Uint8Array(data.buffer)],[1,2,255]);
 await assert.rejects(service.getCharacteristic('unexpected'));await assert.rejects(d.gatt.getPrimaryService('unexpected'));
 let disconnected=0;d.addEventListener('gattserverdisconnected',()=>++disconnected);n.events.emit(JSON.stringify({type:'disconnected',device_id:'one'}));assert.equal(d.gatt.connected,false);assert.equal(disconnected,1);await assert.rejects(char.readValue());a.destroy();b.close();
});
test('disconnect before late native connect cannot resurrect GATT state',async()=>{
 const n=native(),b=new DesktopBridge(n),a=new NativeBluetooth(b,{chooseDevice:async rows=>rows[0].id});const selecting=a.requestDevice(options);n.answer({devices:[{id:'one',name:'CSP-W2-synthetic'}]});const d=await selecting;
 const connecting=d.gatt.connect();const old=n.requests.at(-1);n.events.emit(JSON.stringify({type:'disconnected',device_id:'one'}));await assert.rejects(connecting);n.answer({},true,old);assert.equal(d.gatt.connected,false);assert.equal(a.active,false);a.destroy();b.close();
});
test('cancel during discovery or chooser rejects late selection without connection',async()=>{
 for(const phase of ['discover','chooser']){
  const n=native(),b=new DesktopBridge(n);let choose;const a=new NativeBluetooth(b,{chooseDevice:()=>new Promise(resolve=>choose=resolve)});const p=a.requestDevice(options);
  if(phase==='chooser'){n.answer({devices:[{id:'one',name:'CSP-W2-synthetic'}]});await tick();}
  a.cancel();if(phase==='chooser')choose('one');else n.answer({devices:[{id:'one',name:'CSP-W2-synthetic'}]});await assert.rejects(p);assert.equal(n.requests.filter(r=>r.op==='connect').length,0);a.destroy();b.close();
 }
});
test('malformed native read data fails without exposing received content',async()=>{
 const n=native(),b=new DesktopBridge(n),a=new NativeBluetooth(b,{chooseDevice:async rows=>rows[0].id});const selecting=a.requestDevice(options);n.answer({devices:[{id:'one',name:'CSP-W2-synthetic'}]});const d=await selecting,p=d.gatt.connect();n.answer();await p;const char=await(await d.gatt.getPrimaryService(SERVICE)).getCharacteristic(UUID);
 const read=char.readValue();n.answer({hex:'not-hex'});await assert.rejects(read,e=>e.code==='invalid_payload');a.destroy();n.answer();b.close();
});
test('shared protocol stays pinned while only optional record reads are deferred',async()=>{
 for(const name of ['index.html','style.css','app.mjs','security2.mjs'])assert.deepEqual(await readFile(new URL('../frontend/shared/'+name,import.meta.url)),await readFile(new URL('../../phone/wifi-setup/'+name,import.meta.url)));
 // The approved desktop connection already supports firmware-managed idle
 // sessions. It deliberately differs from the old hosted-phone timer copy.
 const connection=await readFile(new URL('../frontend/shared/connection.mjs',import.meta.url));
 assert.equal(createHash('sha256').update(connection).digest('hex'),'79d12aa9adc562649d6048ca6b151400213e9d65b8f98bc0f906f729e08cd869');
 assert.deepEqual(connection,await readFile(new URL('../../android/app/src/main/assets/shared/connection.mjs',import.meta.url)));
 const original=await readFile(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8'),local=await readFile(new URL('../frontend/app.mjs',import.meta.url),'utf8');
 // The desktop copy now also publishes sanitized wizard facts. All secured
 // client calls and their order remain the existing application operations.
 const operations=/\b(?:flow|cloud|ownerLogin|portal|next|nextCloud)\.(?:abort|save|stage|check|connect|status|profiles|backupStatus|scan|prepare|handoff|cancel|disconnect)\s*\(/g;
 assert.deepEqual(local.match(operations).filter(x=>!x.includes('backupStatus')),original.match(operations).filter(x=>!x.includes('backupStatus')));
 assert.match(local,/async function connectOwner\(\{[^\n]*bluetooth=desktopContext\.bluetooth/);
 assert.match(local,/new ManagementConnection\(\{bluetooth,secureContext:desktopContext\.secureContext/);
 assert.match(local,/from '\.\/shared\/connection\.mjs'/);
 const html=await readFile(new URL('../frontend/index.html',import.meta.url),'utf8');assert.match(html,/connect-src 'none'/);assert.match(html,/script-src 'self';/);assert.doesNotMatch(html,/<(?:script|link)[^>]+(?:src|href)="(?:https?:|qrc:)/);assert.match(html,/src="qwebchannel.js"/);
});
test('actual toolbar construction has unique IDs and does not shadow shared UI controls',async()=>{
 const source=await readFile(new URL('../frontend/bootstrap.mjs',import.meta.url),'utf8'),html=await readFile(new URL('../frontend/index.html',import.meta.url),'utf8');
 class Element {constructor(){this.children=[];}setAttribute(){}append(...items){this.children.push(...items);}replaceChildren(...items){this.children=items;}}
 const root=new Element();root.id='desktop-toolbar';const openSlot=new Element();openSlot.id='wizard-open-slot';const document={createElement:()=>new Element()};
 const start=source.indexOf('function makeToolbar(){'),end=source.indexOf('\nconst toolbar=makeToolbar();',start);
 assert.ok(start>=0&&end>start);new Function('document','$',source.slice(start,end)+';return makeToolbar();')(document,id=>{assert.ok(['desktop-toolbar','wizard-open-slot'].includes(id));return id==='desktop-toolbar'?root:openSlot;});
 const ids=[];function visit(node){if(node.id)ids.push(node.id);for(const child of node.children)if(child.id!=='desktop-open')visit(child);}visit(root);visit(openSlot);ids.push('desktop-open');
 assert.equal(new Set(ids).size,ids.length);const existing=[...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);assert.equal(new Set(existing).size,existing.length);
 for(const id of ids)if(!['desktop-toolbar','wizard-open-slot'].includes(id))assert.ok(!existing.includes(id),'Toolbar shadows '+id);
 assert.ok(ids.includes('desktop-status-message'));assert.ok(ids.includes('desktop-status'));
});
