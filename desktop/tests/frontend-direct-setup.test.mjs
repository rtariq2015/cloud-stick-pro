import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {DirectSetupConnector,savedAccessAvailable} from '../frontend/direct-setup.mjs';
import {DesktopBridge,NativeBluetooth,SERVICE} from '../frontend/bridge.mjs';
import {SetupWizardModel} from '../frontend/wizard-model.mjs';

const key='0123456789abcdef0123456789abcdef',mac='00:11:22:33:44:55';
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
function fixture({access={key,device_id:mac},open={accepted:true},connectResult=true,accessGate,openGate,connectGate,prepare=true}={}){
 const calls=[],returned=access,auth=[];let cancelled=0;
 const bridge={async request(op,args){calls.push([op,args]);if(op==='usb_setup_access'){if(accessGate)await accessGate.promise;return returned;}if(op==='usb_open_setup'){if(openGate)await openGate.promise;return open;}throw Error('Unexpected op');}};
 const bluetooth={async drain(){calls.push(['drain']);},forDevice(id){calls.push(['exact',id]);return {requestDevice(){}};}};
 const connector=new DirectSetupConnector({bridge,bluetooth,prepare(){calls.push(['prepare']);return prepare;},connect(value){auth.push({key:value.ownerKey,expectedDevice:value.expectedDevice,provider:value.bluetooth});auth.argument=value;calls.push(['auth']);return connectGate?connectGate.promise:Promise.resolve(connectResult);},cancel(){++cancelled;}});
 return {connector,calls,returned,auth,get cancelled(){return cancelled;}};
}
test('availability is boolean-only; missing, coerced or extra fields are unavailable',()=>{
 assert.equal(savedAccessAvailable({available:true}),true);
 for(const value of [{available:false},{available:'true'},{available:true,key},null,[],{}])assert.equal(savedAccessAvailable(value),false);
});
test('explicit direct setup drains before key read, opens once, and binds full authenticated identity',async()=>{
 const gate=deferred(),f=fixture({connectGate:gate}),pending=f.connector.start('synthetic-port');await tick();
 assert.deepEqual(f.calls.map(c=>c[0]),['prepare','drain','usb_setup_access','usb_open_setup','exact','auth']);
 assert.equal(f.calls.find(c=>c[0]==='exact')[1],mac);assert.equal(f.auth[0].key,key);assert.equal(f.auth[0].expectedDevice,'001122334455');
 assert.equal(f.returned.key,'');assert.equal(f.returned.device_id,'');assert.equal(f.auth.argument.ownerKey,'');assert.equal(f.auth.argument.bluetooth,null);assert.equal(f.auth.argument.expectedDevice,null);
 assert.equal(JSON.stringify(f.connector),'{}');gate.resolve(true);assert.equal(await pending,true);assert.equal(f.cancelled,0);
});
test('failed readiness never fetches a key or opens a window',async()=>{
 const f=fixture({prepare:false});await assert.rejects(f.connector.start('synthetic-port'));assert.deepEqual(f.calls,[['prepare']]);
});
test('untrusted saved-access response shapes fail before window opening and are cleared',async()=>{
 for(const access of [{key:key.slice(1),device_id:mac},{key,device_id:mac.toLowerCase()+'00'},{key,device_id:'other'},{key,device_id:mac,extra:'untrusted'},null]){
  const f=fixture({access});await assert.rejects(f.connector.start('synthetic-port'),e=>e.code==='setup_access_unavailable');
  assert.equal(f.calls.some(c=>c[0]==='usb_open_setup'),false);assert.equal(f.auth.length,0);if(access)assert.equal(access.key,'');
 }
});
test('window rejection or failed post-auth initialization never succeeds or retries',async()=>{
 for(const options of [{open:{accepted:false}},{connectResult:false}]){
  const f=fixture(options);await assert.rejects(f.connector.start('synthetic-port'));assert.equal(f.calls.filter(c=>c[0]==='usb_setup_access').length,1);assert.equal(f.calls.filter(c=>c[0]==='usb_open_setup').length,1);assert.ok(f.cancelled>=1);assert.equal(f.connector.running,false);
 }
});
test('cancelled late access reply is discarded; no concurrent retry can start',async()=>{
 const gate=deferred(),f=fixture({accessGate:gate}),p=f.connector.start('synthetic-port');await tick();f.connector.cancel();
 assert.equal(f.connector.running,true);await assert.rejects(f.connector.start('synthetic-port'));gate.resolve();await assert.rejects(p);
 assert.equal(f.returned.key,'');assert.equal(f.auth.length,0);assert.equal(f.calls.filter(c=>c[0]==='usb_setup_access').length,1);assert.equal(f.calls.some(c=>c[0]==='usb_open_setup'),false);
});
test('cancel while opening clears access immediately and rejects late completion',async()=>{
 const gate=deferred(),f=fixture({openGate:gate}),p=f.connector.start('synthetic-port');await tick();f.connector.cancel();
 assert.equal(f.returned.key,'');gate.resolve();await assert.rejects(p);assert.equal(f.auth.length,0);assert.equal(f.connector.running,false);
});
test('cancel during authentication closes it and cannot accept late initialized success',async()=>{
 const gate=deferred(),f=fixture({connectGate:gate}),p=f.connector.start('synthetic-port');await tick();f.connector.cancel();gate.resolve(true);await assert.rejects(p);
 assert.ok(f.cancelled>=1);assert.equal(f.auth.argument.ownerKey,'');assert.equal(f.connector.running,false);
});
test('native failure details and credential values never enter fixed public errors',async()=>{
 const f=fixture();f.connector.cancel();const bad=new DirectSetupConnector({bridge:{request(){throw Error(key);}},bluetooth:{drain:async()=>{}},prepare:()=>true,cancel(){},connect(){throw Error(key);}});
 await assert.rejects(bad.start('synthetic-port'),e=>e.code==='setup_access_unavailable'&&!e.message.includes(key));
});
const options={filters:[{namePrefix:'CSP-W2-'}],optionalServices:[SERVICE]};
function nativeFixture(){
 const requests=[],callbacks=new Set(),events=new Set();
 const native={request(raw){requests.push(JSON.parse(raw));},response:{connect:f=>callbacks.add(f),disconnect:f=>callbacks.delete(f)},events:{connect:f=>events.add(f),disconnect:f=>events.delete(f)}};
 const bridge=new DesktopBridge(native),bluetooth=new NativeBluetooth(bridge,{chooseDevice(){throw Error('Chooser must not run for trusted exact target');}});
 return {requests,bridge,bluetooth,answer(value={},row=requests.at(-1)){for(const cb of callbacks)cb(JSON.stringify({id:row.id,ok:true,result:value}));}};
}
test('native exact-target provider selects only returned ID, never first row or chooser',async()=>{
 const f=nativeFixture(),p=f.bluetooth.forDevice(mac).requestDevice(options);f.answer({devices:[{id:'AA:BB:CC:DD:EE:FF',name:'CSP-W2-other'},{id:mac,name:'CSP-W2-334455'}]});
 const d=await p;assert.equal(d.id,mac);const connecting=d.gatt.connect();assert.deepEqual(f.requests.at(-1),{id:2,op:'connect',device_id:mac});f.answer();await connecting;f.bluetooth.destroy();f.answer();f.bridge.close();
});
test('missing exact target cannot fall back to another discovered stick',async()=>{
 const f=nativeFixture(),p=f.bluetooth.forDevice(mac).requestDevice(options);f.answer({devices:[{id:'AA:BB:CC:DD:EE:FF',name:'CSP-W2-334455'}]});await assert.rejects(p);assert.equal(f.requests.some(x=>x.op==='connect'),false);f.bluetooth.destroy();f.bridge.close();
});
test('disconnect publishes loss only after reserving its single drain promise',async()=>{
 const f=nativeFixture(),p=f.bluetooth.forDevice(mac).requestDevice(options);f.answer({devices:[{id:mac,name:'CSP-W2-334455'}]});const d=await p,c=d.gatt.connect();f.answer();await c;
 let drained;d.addEventListener('gattserverdisconnected',()=>{drained=f.bluetooth.drain();});f.bluetooth.cancel();assert.equal(f.requests.filter(x=>x.op==='disconnect').length,1);f.answer();await drained;
 f.bluetooth.destroy();f.bridge.close();
});
test('new strict bridge operations accept only one port and never a supplied key/path',async()=>{
 const f=nativeFixture();for(const op of ['usb_setup_access_status','usb_setup_access','usb_forget_setup_access']){
  const p=f.bridge.request(op,{port:'synthetic-port'});f.answer(op==='usb_setup_access_status'?{available:false}:op==='usb_setup_access'?{key,device_id:mac}:{forgotten:true});await p;
  for(const args of [{},{port:''},{port:'synthetic-port',key},{port:'synthetic-port',path:'/not/allowed'}])await assert.rejects(f.bridge.request(op,args));
 }assert.equal(f.requests.length,3);f.bluetooth.destroy();f.bridge.close();
});
test('wizard direct setup needs a checked owner and full initialization, not raw GATT',()=>{
 const m=new SetupWizardModel();m.next();m.savedAccess(true);assert.equal(m.beginDirect(),false);m.checkedUsb({ownerReady:false});m.savedAccess(true);assert.equal(m.beginDirect(),false);
 m.checkedUsb({ownerReady:true});m.savedAccess(true);assert.equal(m.beginDirect(),true);m.updateSession({busy:true,authenticated:false});assert.equal(m.step,'usb');assert.equal(m.canNext(),false);
 m.updateSession({authenticated:true,loginSupported:true,authenticatedGeneration:1});m.endDirect(true);assert.equal(m.step,'login');assert.equal(m.next(),false);assert.equal(m.savedAccessAvailable,false);assert.doesNotMatch(JSON.stringify(m),/012345|password|ownerKey/);
});
test('saved login reconnect never offers old direct access as a substitute',()=>{
 const m=new SetupWizardModel();m.next();m.checkedUsb({ownerReady:true});m.savedAccess(true);m.next();m.updateSession({authenticated:true,loginSupported:true,authenticatedGeneration:1});m.next();m.updateSession({authenticated:false,loginSaved:true});
 assert.equal(m.loginReconnect,true);m.savedAccess(true);assert.equal(m.beginDirect(),false);assert.equal(m.step,'auth');assert.equal(m.notice,'new-login');
});
async function retirementFixture({result={forgotten:true},failure=false}={}){
 const source=await readFile(new URL('../frontend/bootstrap.mjs',import.meta.url),'utf8'),start=source.indexOf('// A durable Save alone'),end=source.indexOf("addEventListener('pagehide'",start);
 assert.ok(start>=0&&end>start);const handlers=new Map(),calls=[],nodes=new Map(),drain=deferred(),state={authenticated:true},model={loginReconnected:false,finished:false},wizard={model,usbResult:(op,value)=>calls.push([op,value])};
 const get=id=>{if(!nodes.has(id))nodes.set(id,{hidden:true,textContent:''});return nodes.get(id);};
 const control=new Function('addEventListener','wizard','desktopContext','toolbar','bluetooth','bridge','controls','$',`let service=null,retirePort=null,checkedPort='synthetic-port',busy=false,retiring=false,retirementAttempted=false,stopped=false,uiEpoch=0;${source.slice(start,end)};return {invalidate(){++uiEpoch;},get(){return {retirePort,busy,retiring};}};`)(
  (type,callback)=>handlers.set(type,callback),wizard,{getSetupState:()=>state},{ports:{value:'synthetic-port'}},{drain(){calls.push(['drain']);return drain.promise;}},{async request(op,args){calls.push([op,args]);if(failure)throw Error(key);return result;}},()=>{},get);
 return {control,calls,state,model,nodes,drain,signal:()=>handlers.get('cloudstick:setup-state')()};
}
test('bootstrap retirement requires confirmed new-login reconnect, explicit Finish, and native drain',async()=>{
 const f=await retirementFixture();f.signal();assert.equal(f.calls.length,0);
 f.state.authenticated=false;f.model.finished=true;f.signal();assert.equal(f.calls.length,0,'Save/unknown/disconnect without new credential proof cannot retire');
 f.model.finished=false;f.model.loginReconnected=true;f.state.authenticated=true;f.signal();assert.equal(f.calls.length,0,'New credentials while BLE active cannot retire');
 f.model.finished=true;f.state.authenticated=false;f.signal();assert.deepEqual(f.calls,[['drain']]);f.drain.resolve();await tick();
 assert.deepEqual(f.calls,[['drain'],['usb_forget_setup_access',{port:'synthetic-port'}],['usb_setup_access_status',{available:false}]]);assert.equal(f.control.get().retirePort,null);f.signal();assert.equal(f.calls.length,3,'No automatic retry/repetition');
});
test('retirement failure or late completion cannot pretend saved access was cleared',async()=>{
 for(const mode of ['failure','late']){
  const f=await retirementFixture({failure:mode==='failure'});f.model.loginReconnected=true;f.state.authenticated=true;f.signal();f.model.finished=true;f.state.authenticated=false;f.signal();
  if(mode==='late')f.control.invalidate();f.drain.resolve();await tick();assert.equal(f.calls.some(c=>c[0]==='usb_setup_access_status'),false);
  if(mode==='failure'){const note=f.nodes.get('wizard-access-retirement');assert.equal(note.hidden,false);assert.match(note.textContent,/could not confirm/);assert.doesNotMatch(note.textContent,/0123456789/);}
 }
});
