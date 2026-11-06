// Production phone client/script; synthetic DOM/GATT/device service only.
// No physical Bluetooth, NVS, Android rendering or Koofr traffic is exercised.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {CloudSetupClient,CloudInputError,validateCloudFields,ManagementConnection,WifiSetupClient,WifiInputError,
 SERVICE,VERSION,SESSION,INFO,WIFI,validateMetadata} from '../../phone/wifi-setup/connection.mjs';
import {ProtocolError} from '../../phone/wifi-setup/security2.mjs';
const enc=new TextEncoder(),dec=new TextDecoder();
const baseMetadata={prov:{sec_ver:2,sec_patch_ver:1},csp:{milestone:2,synthetic:false,owner:'card',wifi:true,cloud:false}};
const baseStatus={ok:true,revision:'4',configured:true,provider:'koofr',has_username:true,has_password:true,has_folder:true,editing:false,edit:0,store_fault:false};
const publicFields={username:'owner@example.invalid',app_password:'PUBLIC_SYNTHETIC_APP_PASSWORD',folder:'CloudStick/QA'};
function service({status={},failCommit=false,stageError=0,delay='',capable=true,wifiDurable=true}={}){
 let authenticated=false,options,revision='4',transaction=null,edit=0,staged={},commits=0,disconnected=0,release;
 const calls=[];
 const snapshot=()=>({...baseStatus,revision,...status,editing:!!transaction,edit});
 class Connection {
  constructor(next){options=next;}
  async connect(){authenticated=true;const info={device_id:'CSP-W2-SYNTHETIC',firmware:'SYNTHETIC',...(capable?{cloud_setup:true}:{})};options.onState({state:'connected',message:'Authenticated',info});return info;}
  disconnect(){authenticated=false;++disconnected;options?.onState({state:'disconnected',message:'Closed'});}
  async command(op,args={}){
   assert(authenticated,'No application command may run outside the authenticated synthetic session');
   calls.push({op,args:{...args}});
   if(op==='list')return {ok:true,revision:'2',count:0,profiles:[],next:0};
   if(op==='status')return {ok:true,revision:'2',durable:wifiDurable,store_fault:false,wifi:'idle',wifi_error:0,wifi_profile:0,selected_profile:0,applied_revision:'0',internet_verified:false,cloud_available:false,backups_paused:true};
   if(op==='cloud_status'){
    if(delay===op)await new Promise(resolve=>release=resolve);
    return snapshot();
   }
   if(op==='cloud_begin'){assert.equal(args.revision,revision);transaction='22';edit=0;staged={};return {ok:true,revision,transaction,edit,has_username:snapshot().has_username,has_password:snapshot().has_password,has_folder:snapshot().has_folder};}
   if(op==='cloud_stage'){
    assert.equal(args.transaction,transaction);assert.equal(args.edit,edit);
    if(stageError)return {ok:false,error:stageError};staged[args.field]=args.value;++edit;
    return {ok:true,transaction,edit,has_username:Object.hasOwn(staged,'username')||snapshot().has_username,has_password:Object.hasOwn(staged,'app_password')||snapshot().has_password,has_folder:Object.hasOwn(staged,'folder')||snapshot().has_folder};
   }
   if(op==='cloud_commit'){
    assert.equal(args.transaction,transaction);assert.equal(args.edit,edit);assert.equal(args.revision,revision);assert.equal(args.accept_unverified,true);
    ++commits;if(delay===op)await new Promise(resolve=>release=resolve);
    if(failCommit)return {ok:false,error:15,store_fault:true};revision=String(BigInt(revision)+1n);transaction=null;edit=0;
    return {ok:true,durable:true,revision,access_verified:false};
   }
   if(op==='cloud_abort'){assert.equal(args.transaction,transaction);transaction=null;edit=0;staged={};return {ok:true};}
   if(op==='abort')return {ok:true};
   throw new Error('Unexpected synthetic operation '+op);
  }
 }
 return {Connection,calls,snapshot,release:()=>release(),get awaiting(){return !!release;},get commits(){return commits;},get disconnects(){return disconnected;},get revision(){return revision;}};
}
function ui(options={}){
 const device=service(options);
 class Element {hidden=false;disabled=false;checked=false;textContent='';value='';children=[];
  append(...items){this.children.push(...items);}replaceChildren(...items){this.children=items;}
  set innerHTML(_){throw new Error('Unsafe HTML rendering');}}
 const html=readFileSync(new URL('../../phone/wifi-setup/index.html',import.meta.url),'utf8');
 const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(match=>match[1]),e=Object.fromEntries(ids.map(id=>[id,new Element()]));
 // Simulate the actual initial hidden attributes, including the optional form.
 for(const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*\bhidden[^>]*>/g))e[match[1]].hidden=true;
 const windowEvents=new Map(),documentEvents=new Map();
 const document={visibilityState:'visible',getElementById:id=>e[id],createElement:()=>new Element(),createTextNode:text=>({textContent:text}),addEventListener:(name,fn)=>documentEvents.set(name,fn)};
 vm.runInNewContext(readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,''),
  {ManagementConnection:device.Connection,WifiSetupClient,WifiInputError,CloudSetupClient,CloudInputError,validateCloudFields,document,
   location:{protocol:options.protocol??'https:'},isSecureContext:true,navigator:{bluetooth:{requestDevice(){}}},TextEncoder,
   addEventListener:(name,fn)=>windowEvents.set(name,fn)});
 const event=name=>(windowEvents.get(name)||documentEvents.get(name))?.({persisted:true});
 return {...device,e,document,event,device};
}
async function connect(f){f.e['owner-key'].value='PUBLIC_SYNTHETIC_OWNER_KEY_ONLY';await f.e.connect.onclick();}
async function review(f,fields=publicFields){
 f.e['cloud-keep-login'].checked=false;f.e['cloud-keep-login'].onchange();
 f.e['cloud-keep-folder'].checked=false;f.e['cloud-keep-folder'].onchange();
 f.e['cloud-email'].value=fields.username;f.e['cloud-password'].value=fields.app_password;f.e['cloud-folder'].value=fields.folder;
 await f.e['cloud-review'].onclick();
}
async function until(predicate){for(let i=0;i<80&&!predicate();++i)await Promise.resolve();assert(predicate());}
function scrubbed(f){
 assert(f.e['cloud-card'].hidden);assert(f.e['cloud-confirm'].hidden);
 assert(f.e['cloud-keep-login-row'].hidden);assert(f.e['cloud-keep-folder-row'].hidden);
 for(const id of ['cloud-email','cloud-password','cloud-folder'])assert.equal(f.e[id].value,'');
 assert.equal(f.e['cloud-status'].textContent,'');assert.equal(f.e['cloud-message'].textContent,'');
 assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');
}

test('cloud:false metadata remains supported; cloud:true requires exactly protocol1',()=>{
 assert.equal(validateMetadata(baseMetadata).csp.cloud,false);
 assert.equal(validateMetadata({...baseMetadata,csp:{...baseMetadata.csp,cloud:true,cloud_protocol:1}}).csp.cloud,true);
 for(const csp of [{cloud:true},{cloud:true,cloud_protocol:2},{cloud:false,cloud_protocol:1},{cloud:'true'},{cloud:null}])
  assert.throws(()=>validateMetadata({...baseMetadata,csp:{...baseMetadata.csp,...csp}}),ProtocolError);
});
test('actual UI keeps cloud form hidden and sends no cloud calls for old devices',async()=>{
 const f=ui({capable:false});await connect(f);assert(!f.e['wifi-card'].hidden);assert(f.e['cloud-card'].hidden);
 await f.e['cloud-review'].onclick();assert(!f.calls.some(call=>call.op.startsWith('cloud_')));
});
test('actual UI stages each field, requires explicit consent and distinguishes durable save from verified access',async()=>{
 const f=ui();await connect(f);assert(!f.e['cloud-card'].hidden);await review(f);
 assert(!f.e['cloud-confirm'].hidden);assert.equal(f.e['cloud-password'].value,'');assert.equal(f.device.commits,0);
 assert.deepEqual(f.calls.filter(c=>c.op==='cloud_stage').map(c=>[c.args.field,c.args.edit]),[['username',0],['app_password',1],['folder',2]]);
 assert(f.e.scan.disabled);await f.e['cloud-save'].onclick();assert.equal(f.device.commits,0);
 f.e['cloud-accept'].checked=true;await f.e['cloud-save'].onclick();assert.equal(f.device.commits,1);assert.equal(f.device.revision,'5');
 assert.match(f.e['cloud-message'].textContent,/saved and read back at revision 5/);
 assert.match(f.e['cloud-message'].textContent,/not yet verified/);assert(!f.e['cloud-finish'].hidden);
 assert(!f.calls.some(call=>/check|probe|upload|fetch/.test(call.op)));
 f.e['cloud-finish'].onclick();scrubbed(f);assert.match(f.e['connection-message'].textContent,/must safely release setup/);
});
test('keeping saved login stages only the explicitly changed destination',async()=>{
 const f=ui();await connect(f);assert(f.e['cloud-keep-login'].checked);assert(f.e['cloud-email'].disabled);assert(f.e['cloud-password'].disabled);
 f.e['cloud-keep-folder'].checked=false;f.e['cloud-keep-folder'].onchange();f.e['cloud-folder'].value='NewFolder/QA';await f.e['cloud-review'].onclick();
 assert.deepEqual(f.calls.filter(c=>c.op==='cloud_stage').map(c=>c.args.field),['folder']);assert.equal(f.device.commits,0);
 await f.e['cloud-cancel'].onclick();assert.equal(f.device.commits,0);assert.match(f.e['cloud-message'].textContent,/No cloud commit was requested/);
});
test('keeping every saved field never begins or commits a redundant transaction',async()=>{
 const f=ui();await connect(f);await f.e['cloud-review'].onclick();assert(!f.calls.some(c=>['cloud_begin','cloud_commit'].includes(c.op)));
 assert.match(f.e['cloud-message'].textContent,/No changes need saving/);
});
test('cloud editing waits for durable saved Wi-Fi and never auto-checks or auto-saves it',async()=>{
 const f=ui({wifiDurable:false});await connect(f);assert(!f.e['cloud-card'].hidden);assert(f.e['cloud-review'].disabled);
 await review(f);assert(!f.calls.some(c=>['cloud_begin','check','commit'].includes(c.op)));
 assert.match(f.e['cloud-message'].textContent,/First check and save a Wi-Fi/);
});
test('other provider never offers silent credential or folder reuse',async()=>{
 const f=ui({status:{provider:'other',has_username:false,has_password:false,has_folder:false}});await connect(f);
 assert(f.e['cloud-keep-login-row'].hidden);assert(f.e['cloud-keep-folder-row'].hidden);assert.match(f.e['cloud-status'].textContent,/different provider/);
});
for(const reason of ['disconnect','pagehide','pageshow','freeze','resume','visibilitychange'])test(reason+' scrubs cloud inputs, presence, revision and staged state',async()=>{
 const f=ui();await connect(f);await review(f);f.e['cloud-password'].value='PUBLIC_LATE_INPUT';
 if(reason==='disconnect')f.e.disconnect.onclick();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}
 scrubbed(f);assert.equal(f.device.commits,0);assert.equal(f.e['cloud-accept'].checked,false);
});
test('late cloud status after freeze cannot restore the authenticated form',async()=>{
 const f=ui({delay:'cloud_status'});f.e['owner-key'].value='PUBLIC_SYNTHETIC_OWNER_KEY_ONLY';const pending=f.e.connect.onclick();
 await until(()=>f.device.awaiting);f.event('freeze');f.release();await pending;scrubbed(f);
});
test('uncertain commit acknowledgment closes setup without claiming success or unchanged settings',async()=>{
 const f=ui({failCommit:true});await connect(f);await review(f);f.e['cloud-accept'].checked=true;await f.e['cloud-save'].onclick();scrubbed(f);
 assert.match(f.e['cloud-outcome'].textContent,/uncertain/);assert(!/were saved|remain unchanged/.test(f.e['cloud-outcome'].textContent));
 assert.equal(f.device.commits,1);
});
test('disconnect during commit records unknown outcome; late success never repaints authenticated state',async()=>{
 const f=ui({delay:'cloud_commit'});await connect(f);await review(f);f.e['cloud-accept'].checked=true;
 const pending=f.e['cloud-save'].onclick();await until(()=>f.device.awaiting);f.event('freeze');
 assert.match(f.e['cloud-outcome'].textContent,/outcome is unknown/);f.release();await pending;scrubbed(f);
 assert.match(f.e['cloud-outcome'].textContent,/outcome is unknown/);assert.equal(f.device.commits,1);
});
test('revoked transaction response closes setup and clears credentials without a commit',async()=>{
 const f=ui({stageError:13});await connect(f);await review(f);scrubbed(f);assert.equal(f.device.commits,0);
 assert.match(f.e['cloud-outcome'].textContent,/session ended/);
});
test('invalid fields fail before any cloud transaction and without reflecting their contents',async()=>{
 for(const fields of [{...publicFields,username:'not-an-email'},{...publicFields,app_password:''},{...publicFields,folder:'../secret'},{...publicFields,folder:'QA//bad'}]){
  const f=ui();await connect(f);await review(f,fields);assert(!f.calls.some(c=>c.op==='cloud_begin'));assert.equal(f.device.commits,0);
  assert.equal(f.e['cloud-password'].value,'');assert.match(f.e['cloud-message'].textContent,/Check the email/);
 }
});
test('HTTP refuses key and cloud inputs and creates no connection',async()=>{
 const f=ui({protocol:'http:'});await connect(f);assert(f.e.connect.disabled);assert(f.e['owner-key'].disabled);assert(f.e['cloud-email'].disabled);
 assert(f.e['cloud-card'].hidden);assert.equal(f.calls.length,0);
});

test('cloud validator accepts bounded UTF-8 and safe folders, rejects control, overflow, URLs and unknown fields',()=>{
 assert.doesNotThrow(()=>validateCloudFields({username:'é'.repeat(64),app_password:'\\'.repeat(128),folder:'a'.repeat(96)}));
 for(const fields of [{},{username:'a'.repeat(129)},{app_password:'é'.repeat(65)},{app_password:'secret\nline'},{username:'x:y'},
  {folder:'/QA'},{folder:'QA/'},{folder:'QA/..'},{folder:'QA/.'},{folder:'QA//x'},{folder:'QA/é'},{folder:'https://example.invalid'},{folder:'a'.repeat(97)},
  {folder:'QA',ca:'-----PRIVATE-----'},{app_password:'\ud800'}])assert.throws(()=>validateCloudFields(fields),CloudInputError);
});
test('client rejects secret-bearing or inconsistent status, staging and commit replies',async()=>{
 for(const bad of [{...baseStatus,password:'PRIVATE'},{...baseStatus,provider:'other'},{...baseStatus,revision:'0'},{...baseStatus,has_password:'yes'}]){
  let closed=0;const client=new CloudSetupClient({command:async()=>bad,disconnect(){++closed;}});
  await assert.rejects(client.status(),ProtocolError);assert.equal(closed,1);
 }
 for(const response of [{ok:true,durable:true,revision:'6',access_verified:false},{ok:true,durable:true,revision:'5',access_verified:true},
  {ok:true,durable:false,revision:'5',access_verified:false},{ok:true,durable:true,revision:'5',access_verified:false,password:'PRIVATE'}]){
  const f=service();const connection=new f.Connection({onState(){}});await connection.connect();const actual=connection.command.bind(connection);
  connection.command=async(op,args)=>op==='cloud_commit'?response:actual(op,args);
  const client=new CloudSetupClient(connection);await client.status();await client.stage(publicFields);await assert.rejects(client.save({acceptUnverified:true}),ProtocolError);
 }
});
test('client enforces consent and candidate before sending cloud_commit',async()=>{
 const f=service(),connection=new f.Connection({onState(){}});await connection.connect();const client=new CloudSetupClient(connection);
 await assert.rejects(client.save(),error=>error instanceof CloudInputError&&error.code===10);
 await assert.rejects(client.save({acceptUnverified:true}),error=>error instanceof CloudInputError&&error.code===4);
 assert(!f.calls.some(c=>c.op==='cloud_commit'));
});
test('client rejects a changed transaction, skipped edit or secret echo during staging',async()=>{
 for(const change of [{transaction:'23'},{edit:8},{app_password:'PRIVATE'}]){
  const f=service(),connection=new f.Connection({onState(){}});await connection.connect();const actual=connection.command.bind(connection);
  connection.command=async(op,args)=>{const value=await actual(op,args);return op==='cloud_stage'?{...value,...change}:value;};
  const client=new CloudSetupClient(connection);await client.status();await assert.rejects(client.stage(publicFields),ProtocolError);
  assert.equal(f.commits,0);assert.equal(f.calls.filter(c=>c.op==='cloud_stage').length,1);
 }
});

function gatt({cloud=true,cloudProtocol=1,failAuthentication=false}={}){
 const events=new Map(),calls=[];let authorized=false,step=0,job=0,currentCloud=cloud,currentProtocol=cloudProtocol;
 const characteristics=Object.fromEntries([VERSION,SESSION,INFO,WIFI].map(uuid=>{let cache=new Uint8Array();return [uuid,{
  async writeValueWithResponse(payload){assert(payload.length<=512);if(uuid===VERSION){assert.equal(dec.decode(payload),'---');cache=enc.encode(JSON.stringify({...baseMetadata,csp:{...baseMetadata.csp,cloud:currentCloud,...(currentCloud?{cloud_protocol:currentProtocol}:{})}}));return;}
   if(uuid===SESSION){cache=Uint8Array.of(++step===1?2:4);return;}
   assert(authorized,'Application write before authentication');const request=JSON.parse(dec.decode(payload));
   if(uuid===INFO){cache=enc.encode(JSON.stringify({v:1,id:request.id,ok:true,model:'SYNTHETIC',device_id:'CSP-W2-SYNTHETIC',firmware:'SYNTHETIC',wifi_connected:false,backups_paused:true,synthetic:false}));return;}
   calls.push(request);cache=enc.encode(JSON.stringify({v:2,id:request.id,ok:true,job:++job,pending:false,data:baseStatus}));
  },async readValue(){return new DataView(cache.buffer,cache.byteOffset,cache.byteLength);},clear(){cache=new Uint8Array();}}];}));
 const device={gatt:{connected:false,async connect(){this.connected=true;return {async getPrimaryService(id){assert.equal(id,SERVICE);return {async getCharacteristic(id){return characteristics[id];}};}};},
  disconnect(){this.connected=false;authorized=false;step=0;for(const c of Object.values(characteristics))c.clear();}},addEventListener:(name,fn)=>events.set(name,fn),removeEventListener:name=>events.delete(name)};
 const connection=new ManagementConnection({secureContext:true,sessionTimeoutMs:10000,bluetooth:{async requestDevice(){return device;}},securityFactory:()=>({async start(){return Uint8Array.of(1);},async challenge(){return Uint8Array.of(3);},
  finish(){if(failAuthentication)throw new ProtocolError('Synthetic owner rejection');authorized=true;},async encrypt(value){assert(authorized);return value.slice();},async decrypt(value){assert(authorized);return value.slice();},destroy(){authorized=false;}})});
 return {connection,calls,legacy(){currentCloud=false;currentProtocol=undefined;},close:()=>connection.disconnect()};
}
test('actual connection allows cloud mailbox only after authenticated protocol1 capability',async()=>{
 const f=gatt();try{await assert.rejects(f.connection.command('cloud_status'),/Connect/);const info=await f.connection.connect();assert.equal(info.cloud_setup,true);
  assert.equal((await new CloudSetupClient(f.connection).status()).revision,'4');assert.deepEqual(f.calls.map(c=>c.op),['cloud_status']);
  await assert.rejects(f.connection.command('cloud_status',{op:'cloud_commit'}),/Unsupported/);
 }finally{f.close();}
});
test('failed owner authentication, unsupported protocol and old devices never admit cloud commands',async()=>{
 for(const options of [{failAuthentication:true},{cloudProtocol:2},{cloud:false}]){
  const f=gatt(options);try{if(options.cloud===false){const info=await f.connection.connect();assert.notEqual(info.cloud_setup,true);await assert.rejects(f.connection.command('cloud_status'),/Unsupported/);}
   else{await assert.rejects(f.connection.connect());await assert.rejects(f.connection.command('cloud_status'),/Connect/);}assert.equal(f.calls.length,0);
  }finally{f.close();}
 }
});
test('reconnection clears prior cloud admission when legacy firmware is encountered',async()=>{
 const f=gatt();try{await f.connection.connect();await f.connection.command('cloud_status');f.close();f.legacy();
  const info=await f.connection.connect();assert.notEqual(info.cloud_setup,true);await assert.rejects(f.connection.command('cloud_status'),/Unsupported/);assert.equal(f.calls.length,1);
 }finally{f.close();}
});
test('maximum escaped staged password remains bounded by real mailbox plaintext and GATT limits',async()=>{
 const f=gatt();try{await f.connection.connect();await f.connection.command('cloud_stage',{transaction:'18446744073709551615',edit:4294967294,field:'app_password',value:'\\'.repeat(128)});
  assert(enc.encode(JSON.stringify(f.calls[0])).length<=496);
 }finally{f.close();}
});
test('the five-file page keeps CSP connect-none and has no secret upload/storage path',()=>{
 const root=new URL('../../phone/wifi-setup/',import.meta.url),html=readFileSync(new URL('index.html',root),'utf8'),app=readFileSync(new URL('app.mjs',root),'utf8');
 assert.match(html,/connect-src 'none'/);assert.match(html,/form-action 'none'/);assert.match(html,/type="password"[^>]*autocomplete="off"/);
 assert(!/\b(fetch|XMLHttpRequest|WebSocket|localStorage|sessionStorage|sendBeacon)\b/.test(app));
 assert(!/\.innerHTML\s*=|console\./.test(app));
});
