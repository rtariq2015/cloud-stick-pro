// Actual connection/client with synthetic GATT/security and owner-record store.
// These admission/race tests do not establish RF, NVS or SDK provisioning.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {OwnerLoginClient,OwnerLoginInputError,ManagementConnection,validateMetadata,SERVICE,VERSION,SESSION,INFO,WIFI} from '../../phone/wifi-setup/connection.mjs';
import {ProtocolError} from '../../phone/wifi-setup/security2.mjs';
const vector=JSON.parse(readFileSync(new URL('./reference-vectors.json',import.meta.url))).vectors[0];
const record=()=>Uint8Array.from(Buffer.from(vector.record,'hex'));
const enc=new TextEncoder(),dec=new TextDecoder();
const metadata={prov:{sec_ver:2,sec_patch_ver:1},csp:{milestone:2,synthetic:false,owner:'card',wifi:true,cloud:false,owner_login:true,owner_login_protocol:1}};
function store({commitFault=false,change={},fail=0}={}){
 const calls=[];let edit=0,received=0,writes=0,active=false,disconnected=0;
 const reply=(overrides={})=>({ok:true,error:0,transaction:'99',revision:'1001',edit,received,device:vector.device,durable:false,reconnect_required:false,store_fault:false,...overrides});
 const connection={disconnect(){++disconnected;},async command(op,args={}){
  calls.push({op,args:{...args}});if(fail)return {ok:false,error:fail,store_fault:false};
  if(op==='login_begin'){assert(!active);active=true;edit=0;received=0;return reply();}
  assert(active,'No active synthetic transaction');assert.equal(args.transaction,'99');
  if(op==='login_stage'){assert.equal(args.edit,edit);assert.equal(args.offset,received);assert.match(args.data,/^[0-9a-f]+$/);const size=args.data.length/2;assert(size>0&&size<=128);++edit;received+=size;return {...reply(),...change};}
  if(op==='login_commit'){assert.equal(received,432);assert.equal(args.revision,'1001');assert.equal(args.edit,edit);assert.equal(args.accept,true);++writes;
   if(commitFault)return {ok:false,error:15,store_fault:true};active=false;return {...reply({revision:'427',durable:true,reconnect_required:true}),...change};}
  if(op==='login_abort'){active=false;edit=received=0;return reply();}throw Error('Unexpected fixture operation');
 }};
 return {connection,calls,get writes(){return writes;},get disconnected(){return disconnected;}};
}
test('strict login metadata is optional and protocol1-only',()=>{
 assert.equal(validateMetadata(metadata).csp.owner_login,true);
 const {owner_login,owner_login_protocol,...old}=metadata.csp;assert.doesNotThrow(()=>validateMetadata({...metadata,csp:old}));
 for(const replacement of [{owner_login:true},{owner_login:true,owner_login_protocol:2},{owner_login:false,owner_login_protocol:1},{owner_login:'true'}]){
  assert.throws(()=>validateMetadata({...metadata,csp:{...old,...replacement}}),ProtocolError);
 }
});
test('record stages in four exact contiguous chunks; explicit consent and fingerprint change commit once',async()=>{
 const f=store(),working=record(),client=new OwnerLoginClient(f.connection,{recordFactory:async()=>working});
 await client.stage({username:vector.username,password:vector.password});assert(working.every(value=>value===0));
 assert.deepEqual(f.calls.filter(c=>c.op==='login_stage').map(c=>[c.args.edit,c.args.offset,c.args.data.length/2]),[[0,0,128],[1,128,128],[2,256,128],[3,384,48]]);
 assert(!JSON.stringify(f.calls).includes(vector.password));assert(!JSON.stringify(f.calls).includes('username'));
 await assert.rejects(client.save(),error=>error instanceof OwnerLoginInputError&&error.code===10);assert.equal(f.writes,0);
 assert.deepEqual(await client.save({accept:true}),{durable:true,reconnect_required:true});assert.equal(f.writes,1);
 await assert.rejects(client.save({accept:true}),/Reconnect/);await assert.rejects(client.abort(),/Reconnect/);assert.equal(f.writes,1);
});
test('cancel stages no persistent replacement',async()=>{
 const f=store(),client=new OwnerLoginClient(f.connection,{recordFactory:async()=>record()});await client.stage(vector);await client.abort();assert.equal(f.writes,0);assert.equal(f.calls.at(-1).op,'login_abort');
 await assert.rejects(client.save({accept:true}),error=>error instanceof OwnerLoginInputError&&error.code===4);
});
test('unknown write outcome is explicit and does not retry',async()=>{
 const f=store({commitFault:true}),client=new OwnerLoginClient(f.connection,{recordFactory:async()=>record()});await client.stage(vector);
 await assert.rejects(client.save({accept:true}),error=>error instanceof OwnerLoginInputError&&error.uncertain&&error.code===15);assert.equal(f.writes,1);
 await assert.rejects(client.save({accept:true}),/Reconnect/);await assert.rejects(client.abort(),/Reconnect/);assert.equal(f.writes,1);
});
for(const [name,change] of [['transaction',{transaction:'98'}],['device',{device:'001122334456'}],['skipped edit',{edit:8}],['received',{received:10}],['secret echo',{password:'PRIVATE'}]])
 test('rejects '+name+' stage response and stops chunk delivery',async()=>{
  const f=store({change}),working=record(),client=new OwnerLoginClient(f.connection,{recordFactory:async()=>working});await assert.rejects(client.stage(vector),ProtocolError);
  assert.equal(f.calls.filter(c=>c.op==='login_stage').length,1);assert.equal(f.writes,0);assert.equal(f.disconnected,1);assert(working.every(value=>value===0));
 });
test('cancelled asynchronous generation and cancelled first chunk never reach commit or a later chunk',async()=>{
 for(const phase of ['generation','stage']){
  let cancelled=false,release;const f=store(),actual=f.connection.command.bind(f.connection),working=record();
  if(phase==='stage')f.connection.command=async(op,args)=>{const result=await actual(op,args);if(op==='login_stage')cancelled=true;return result;};
  const client=new OwnerLoginClient(f.connection,{recordFactory:async()=>{if(phase==='generation')await new Promise(resolve=>release=resolve);return working;}});
  const pending=client.stage({...vector,cancelled:()=>cancelled});if(phase==='generation'){await until(()=>!!release);cancelled=true;release();}
  await assert.rejects(pending,/ended/);assert.equal(f.calls.filter(c=>c.op==='login_stage').length,phase==='generation'?0:1);assert.equal(f.writes,0);assert(working.every(value=>value===0));
 }
});
for(const [name,change] of [['same fingerprint',{revision:'1001'}],['not durable',{durable:false}],['no reconnect',{reconnect_required:false}],['bad receive',{received:431}]])
 test('rejects '+name+' commit acknowledgment',async()=>{
  const f=store(),client=new OwnerLoginClient(f.connection,{recordFactory:async()=>record()});await client.stage(vector);
  const actual=f.connection.command.bind(f.connection);f.connection.command=async(op,args)=>{const answer=await actual(op,args);return op==='login_commit'?{...answer,...change}:answer;};
  await assert.rejects(client.save({accept:true}),ProtocolError);assert.equal(f.writes,1);assert.equal(f.disconnected,1);
 });

async function until(predicate){for(let i=0;i<80&&!predicate();++i)await Promise.resolve();assert(predicate());}
function gatt({capable=true,failAuthentication=false,delayOldStatus=false,holdCommit=false}={}){
 const events=new Map(),calls=[],states=[];let auth=false,step=0,job=0,current=null,statusWaiting=false,statusRelease,commitRelease=!holdCommit,commitWaiting=false;
 const model=store();const characteristics=Object.fromEntries([VERSION,SESSION,INFO,WIFI].map(uuid=>{let cache=new Uint8Array();return [uuid,{
  async writeValueWithResponse(payload){assert(payload.length<=512);if(uuid===VERSION){assert.equal(dec.decode(payload),'---');const {owner_login,owner_login_protocol,...old}=metadata.csp;
   cache=enc.encode(JSON.stringify({...metadata,csp:capable?metadata.csp:old}));return;}
   if(uuid===SESSION){cache=Uint8Array.of(++step===1?2:4);return;}assert(auth,'Application command before owner authentication');const request=JSON.parse(dec.decode(payload));
   if(uuid===INFO){cache=enc.encode(JSON.stringify({v:1,id:request.id,ok:true,model:'SYNTHETIC',device_id:'CSP-W2-'+vector.device,firmware:'SYNTHETIC',wifi_connected:false,backups_paused:true,synthetic:false}));return;}
   calls.push(request);
   if(request.op==='result'){assert(current&&request.job===current.job);if(current.op==='login_commit'&&!commitRelease){cache=enc.encode(JSON.stringify({v:2,id:request.id,ok:true,job:current.job,pending:true}));return;}
    const data=await model.connection.command(current.op,current.args);cache=enc.encode(JSON.stringify({v:2,id:request.id,ok:true,job:current.job,pending:false,data}));current=null;return;}
   if(request.op==='login_commit'&&holdCommit){commitWaiting=true;current={job:++job,op:request.op,args:request};cache=enc.encode(JSON.stringify({v:2,id:request.id,ok:true,job,pending:true}));return;}
   const data=request.op==='status'?{ok:true,synthetic:'old-job-result'}:await model.connection.command(request.op,request);
   cache=enc.encode(JSON.stringify({v:2,id:request.id,ok:true,job:++job,pending:false,data}));
  },async readValue(){if(uuid===WIFI&&delayOldStatus&&!statusWaiting&&calls.at(-1)?.op==='status'){statusWaiting=true;await new Promise(resolve=>statusRelease=resolve);}
   return new DataView(cache.buffer,cache.byteOffset,cache.byteLength);},clear(){cache=new Uint8Array();}}];}));
 const device={gatt:{connected:false,async connect(){this.connected=true;return {async getPrimaryService(id){assert.equal(id,SERVICE);return {async getCharacteristic(id){return characteristics[id];}};}};},
  disconnect(){this.connected=false;auth=false;step=0;for(const characteristic of Object.values(characteristics))characteristic.clear();}},addEventListener:(name,fn)=>events.set(name,fn),removeEventListener:name=>events.delete(name)};
 const connection=new ManagementConnection({secureContext:true,sessionTimeoutMs:10000,onState:state=>states.push(state),bluetooth:{requestDevice:async()=>device},securityFactory:()=>({
  async start(){return Uint8Array.of(1);},async challenge(){return Uint8Array.of(3);},finish(){if(failAuthentication)throw new ProtocolError('Synthetic auth rejected');auth=true;},
  async encrypt(value){assert(auth);return value.slice();},async decrypt(value){assert(auth);return value.slice();},destroy(){auth=false;}})});
 return {connection,model,calls,states,device,client:new OwnerLoginClient(connection,{recordFactory:async()=>record()}),get statusWaiting(){return statusWaiting;},get commitWaiting(){return commitWaiting;},releaseStatus:()=>statusRelease(),releaseCommit:()=>commitRelease=true,close:()=>connection.disconnect()};
}
test('capability and authentication gate login requests before GATT writes',async()=>{
 for(const options of [{capable:false},{failAuthentication:true}]){
  const f=gatt(options);try{await assert.rejects(f.connection.command('login_begin'),/Connect/);if(options.failAuthentication)await assert.rejects(f.connection.connect());else{const info=await f.connection.connect();assert.notEqual(info.owner_login,true);await assert.rejects(f.connection.command('login_begin'),/Unsupported/);}
   assert.equal(f.calls.length,0);
  }finally{f.close();}
 }
});
test('reservation drains an admitted old result and rejects queued refresh without dropping it',async()=>{
 const f=gatt({delayOldStatus:true});try{
  await f.connection.connect();const old=f.connection.command('status');await until(()=>f.statusWaiting);
  const stage=f.client.stage(vector);await Promise.resolve();const queued=old.then(()=>f.connection.refresh());const queuedDenied=assert.rejects(queued,/login change/);
  await assert.rejects(f.connection.refresh(),/login change/);await assert.rejects(f.connection.command('status'),/login change/);
  assert.deepEqual(f.calls.map(c=>c.op),['status']);assert(f.device.gatt.connected);
  f.releaseStatus();assert.equal((await old).synthetic,'old-job-result');await queuedDenied;await stage;
  assert.deepEqual(f.calls.map(c=>c.op),['status','login_begin','login_stage','login_stage','login_stage','login_stage']);assert(f.device.gatt.connected);
  await f.client.abort();assert.equal((await f.connection.command('status')).synthetic,'old-job-result');
 }finally{f.close();}
});
test('pending commit permits only its result polls; blocked refresh/status never loses ACK',async()=>{
 const f=gatt({holdCommit:true});try{
  await f.connection.connect();await f.client.stage(vector);const saving=f.client.save({accept:true});await until(()=>f.commitWaiting);
  await assert.rejects(f.connection.refresh(),/login change/);await assert.rejects(f.connection.command('status'),/login change/);assert(f.device.gatt.connected);
  await assert.rejects(f.connection.command('login_commit'),/login save is pending/);await assert.rejects(f.connection.command('login_abort'),/login save is pending/);
  f.releaseCommit();assert.equal((await saving).durable,true);assert(f.calls.some(c=>c.op==='result'));
  await assert.rejects(f.connection.command('status'),/Login changed/);await assert.rejects(f.connection.refresh(),/Login changed/);
  assert(!f.calls.some(c=>c.op==='status'));assert(f.device.gatt.connected);assert.equal(f.model.writes,1);
 }finally{f.close();}
});
test('disconnect while waiting for old result cancels reservation without starting login',async()=>{
 const f=gatt({delayOldStatus:true});try{
  await f.connection.connect();const old=f.connection.command('status');await until(()=>f.statusWaiting);const preparing=f.client.stage(vector);
  f.close();f.releaseStatus();await assert.rejects(old,/closed/);await assert.rejects(preparing,/closed/);assert(!f.calls.some(c=>c.op==='login_begin'));
 }finally{f.close();}
});
