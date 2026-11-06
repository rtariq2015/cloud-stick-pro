import {test} from 'node:test';
import assert from 'node:assert/strict';
import {NativeRPC,NativeBluetooth,SERVICE} from '../app/src/main/assets/native.mjs';
import {ManagementConnection,WifiSetupClient,FileHistoryClient,VERSION,SESSION,INFO,WIFI} from '../app/src/main/assets/shared/connection.mjs';
const id='AA:BB:CC:DD:EE:01',name='CSP-W2-SYNTHETIC',devices=[{id,name}];
const options={filters:[{namePrefix:'CSP-W2-'}],optionalServices:[SERVICE]};
const encode=new TextEncoder(),decode=new TextDecoder();
const asHex=bytes=>Buffer.from(bytes).toString('hex');
function fakeRpc(handler=async()=>({})){const calls=[],events=[];return {calls,events,onEvent(fn){events.push(fn);},async request(op,args={}){calls.push({op,...args});return handler(op,args);}};}
function inventory(op){return op==='discover'?{devices}:{};}
test('actual RPC serializes exact scalar requests and returns read hex',async()=>{
 const endpoint={postMessage(raw){const request=JSON.parse(raw);assert.deepEqual(request,{id:1,op:'read',uuid:INFO});queueMicrotask(()=>endpoint.onmessage({data:JSON.stringify({id:1,ok:true,result:{hex:'00ff'}})}));}};
 const rpc=new NativeRPC(endpoint);assert.deepEqual(await rpc.request('read',{uuid:INFO}),{hex:'00ff'});
});
test('RPC ignores malformed, wrong-id and extra-field responses',async()=>{
 const endpoint={postMessage(raw){const {id}=JSON.parse(raw);for(const response of ['not-json',JSON.stringify({id:99,ok:true,result:{}}),JSON.stringify({id,ok:true,result:{},private:'unexpected'})])endpoint.onmessage({data:response});queueMicrotask(()=>endpoint.onmessage({data:JSON.stringify({id,ok:true,result:{}})}));}};
 assert.deepEqual(await new NativeRPC(endpoint).request('disconnect'),{});
});
test('RPC accepts only fixed sanitized native error codes',async()=>{
 const endpoint={postMessage(raw){const {id}=JSON.parse(raw);queueMicrotask(()=>endpoint.onmessage({data:JSON.stringify({id,ok:false,error:'bluetooth_busy'})}));}};
 await assert.rejects(new NativeRPC(endpoint).request('discover'),/bluetooth_busy/);
});
test('RPC timeout requests native disconnect and rejects late response',async()=>{
 const calls=[];let endpoint={postMessage(raw){const value=JSON.parse(raw);calls.push(value.op);if(value.op==='disconnect')queueMicrotask(()=>endpoint.onmessage({data:JSON.stringify({id:value.id,ok:true,result:{}})}));}};
 const rpc=new NativeRPC(endpoint,{timeout:10});await assert.rejects(rpc.request('read',{uuid:INFO}),/timed out/);assert.deepEqual(calls,['read','disconnect']);endpoint.onmessage({data:JSON.stringify({id:1,ok:true,result:{hex:'00'}})});
});
test('explicit user selection is required and exact selected ID retained',async()=>{
 const rpc=fakeRpc(inventory),bluetooth=new NativeBluetooth(rpc,async list=>{assert.deepEqual(list,devices);return id;});
 const device=await bluetooth.requestDevice(options);assert.equal(device.id,id);await device.gatt.connect();assert.deepEqual(rpc.calls,[{op:'discover'},{op:'connect',device_id:id}]);await bluetooth.close();
});
test('no first-device fallback and no arbitrary chosen target',async()=>{
 const rpc=fakeRpc(inventory),bluetooth=new NativeBluetooth(rpc,async()=>null);await assert.rejects(bluetooth.requestDevice(options),{name:'NotFoundError'});assert.equal(rpc.calls.filter(c=>c.op==='connect').length,0);
});
test('empty and hostile discovery inventory rejected',async()=>{
 for(const value of [[],[{id,name:'unreviewed'}],[{id:'arbitrary',name}],Array(17).fill({id,name}),[{id,name,private:'secret'}]]){
  const bluetooth=new NativeBluetooth(fakeRpc(async()=>({devices:value})),async()=>id);await assert.rejects(bluetooth.requestDevice(options));
 }
});
test('cancel during chooser prevents any connect',async()=>{
 let choose;const rpc=fakeRpc(inventory),bluetooth=new NativeBluetooth(rpc,()=>new Promise(resolve=>{choose=resolve;}));const pending=bluetooth.requestDevice(options);
 while(!choose)await Promise.resolve();await bluetooth.close();choose(id);await assert.rejects(pending);assert.equal(rpc.calls.filter(c=>c.op==='connect').length,0);
});
test('late native connection success is disconnected and cannot authenticate',async()=>{
 let release;const rpc=fakeRpc(async op=>op==='discover'?{devices}:op==='connect'?new Promise(resolve=>{release=resolve;}):{});
 const bluetooth=new NativeBluetooth(rpc,async()=>id),device=await bluetooth.requestDevice(options);const connect=device.gatt.connect();while(!release)await Promise.resolve();await bluetooth.close();release({});await assert.rejects(connect,/cancelled/);assert.equal(device.gatt.connected,false);assert.equal(rpc.calls.filter(c=>c.op==='write').length,0);
});
test('new discovery awaits old native disconnect drain',async()=>{
 let release;const rpc=fakeRpc(async op=>op==='disconnect'?new Promise(resolve=>{release=resolve;}):{devices});const bluetooth=new NativeBluetooth(rpc,async()=>id);
 const closing=bluetooth.close(),next=bluetooth.requestDevice(options);await Promise.resolve();await Promise.resolve();assert.equal(rpc.calls.filter(c=>c.op==='discover').length,0);release({});await closing;await next;assert.equal(rpc.calls.filter(c=>c.op==='discover').length,1);
});
test('one full512 attribute and read response forwarded unchanged',async()=>{
 const rpc=fakeRpc(async op=>op==='discover'?{devices}:op==='read'?{hex:'ab'.repeat(512)}:{}),bluetooth=new NativeBluetooth(rpc,async()=>id),device=await bluetooth.requestDevice(options);await device.gatt.connect();const service=await device.gatt.getPrimaryService(SERVICE),characteristic=await service.getCharacteristic(INFO);
 const payload=new Uint8Array(512).fill(0xcd);await characteristic.writeValueWithResponse(payload);const view=await characteristic.readValue();assert.equal(view.byteLength,512);assert.equal(rpc.calls.filter(c=>c.op==='write').length,1);assert.equal(rpc.calls.find(c=>c.op==='write').hex,'cd'.repeat(512));await assert.rejects(characteristic.writeValueWithResponse(new Uint8Array(513)));await bluetooth.close();
});
test('native disconnect destroys retained device and notifies once',async()=>{
 const rpc=fakeRpc(inventory),bluetooth=new NativeBluetooth(rpc,async()=>id),device=await bluetooth.requestDevice(options);await device.gatt.connect();let ended=0;device.addEventListener('gattserverdisconnected',()=>ended++);rpc.events[0]({type:'disconnected'});assert.equal(device.gatt.connected,false);assert.equal(ended,1);rpc.events[0]({type:'disconnected'});assert.equal(ended,1);
});
test('actual shared connection probes metadata by nonempty write before read',async()=>{
 const metadata={prov:{sec_ver:2,sec_patch_ver:1},csp:{milestone:2,synthetic:false,owner:'card',wifi:true,cloud:false,backup_status:true,file_history:true,file_history_protocol:1}};
 let last=new Map(),secureDestroyed=0;const commands=[];
 const rpc=fakeRpc(async(op,args)=>{
  if(op==='discover')return {devices};
  if(op==='write'){last.set(args.uuid,Buffer.from(args.hex,'hex'));if(args.uuid===WIFI)commands.push(JSON.parse(decode.decode(last.get(WIFI))).op);return {};}
  if(op==='read'){
   assert(last.has(args.uuid),'Read must follow nonempty response-write');let bytes;
   if(args.uuid===VERSION){assert.equal(decode.decode(last.get(VERSION)),'---');bytes=encode.encode(JSON.stringify(metadata));}
   else if(args.uuid===SESSION)bytes=new Uint8Array([2]);
   else if(args.uuid===INFO){const request=JSON.parse(decode.decode(last.get(INFO)));bytes=encode.encode(JSON.stringify({v:1,id:request.id,ok:true,model:'synthetic-model',device_id:'synthetic-id',firmware:'0.4.1-setup-dev',wifi_connected:true,backups_paused:true,synthetic:false}));}
   else {const request=JSON.parse(decode.decode(last.get(WIFI))),data=request.op==='backup_status'?{ok:true,cloud_configured:true,last_backup_unix:123,verified:10,pending:0,failed:0,backup_error:0,backups_paused:true}:{ok:true,g:1,n:0,matched:0,when:'123',loaded:true,complete:true,limited:false,corrupt:false,io_failed:false,allocation_failed:false,exhausted:false};bytes=encode.encode(JSON.stringify({v:2,id:request.id,ok:true,job:1,pending:false,data}));}
   return {hex:asHex(bytes)};
  }return {};
 });
 const bluetooth=new NativeBluetooth(rpc,async()=>id),security={async start(){return new Uint8Array(407).fill(1);},async challenge(){return new Uint8Array([3]);},finish(){},async encrypt(bytes){return bytes.slice();},async decrypt(bytes){return bytes.slice();},destroy(){secureDestroyed++;}};
 const connection=new ManagementConnection({bluetooth,secureContext:true,securityFactory:()=>security,sessionTimeoutMs:null});
 const info=await connection.connect();assert.equal(info.file_history,true);assert.equal((await new WifiSetupClient(connection).backupStatus()).verified,10);assert.deepEqual((await new FileHistoryClient(connection).read()).rows,[]);
 assert.deepEqual(commands,['backup_status','history_status']);assert.equal(rpc.calls.find(c=>c.op==='write'&&c.uuid===SESSION).hex.length,814);
 connection.disconnect();await bluetooth.drain();assert.equal(secureDestroyed,1);await assert.rejects(connection.refresh(),/No authenticated session/);
});
