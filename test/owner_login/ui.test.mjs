// Actual page script and OwnerLoginClient, synthetic DOM/GATT-facing service.
// No physical adapter, password manager, device state or cloud account is used.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {WifiSetupClient,WifiInputError,CloudSetupClient,CloudInputError,validateCloudFields,OwnerLoginClient as ActualOwnerLoginClient,OwnerLoginInputError,OwnerRecordError,validateOwnerLogin} from '../../phone/wifi-setup/connection.mjs';
const vector=JSON.parse(readFileSync(new URL('./reference-vectors.json',import.meta.url))).vectors[0];
const html=readFileSync(new URL('../../phone/wifi-setup/index.html',import.meta.url),'utf8');
function fixture({capable=true,failAuthentication=false,commitFault=false,delay='',protocol='https:'}={}){
 class Element {hidden=false;disabled=false;checked=false;textContent='';value='';children=[];
  append(...items){this.children.push(...items);}replaceChildren(...items){this.children=items;}
  set innerHTML(_){throw Error('Unsafe markup');}}
 const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(match=>match[1]),e=Object.fromEntries(ids.map(id=>[id,new Element()]));
 for(const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*\bhidden[^>]*>/g))e[match[1]].hidden=true;
 const windowEvents=new Map(),documentEvents=new Map(),options=[],calls=[];
 const document={visibilityState:'visible',getElementById:id=>e[id],createElement:()=>new Element(),createTextNode:text=>({textContent:text}),addEventListener:(name,fn)=>documentEvents.set(name,fn)};
 let onState,authenticated=false,edit=0,received=0,commits=0,release,recordWorking=null;
 const answer=(overrides={})=>({ok:true,error:0,transaction:'99',revision:'1001',edit,received,device:vector.device,durable:false,reconnect_required:false,store_fault:false,...overrides});
 class Connection {
  constructor(value){options.push(value);onState=value.onState;assert.equal(e['login-password'].value,'');assert.equal(e['owner-key'].value,'');}
  async connect(){if(failAuthentication){onState({state:'error',message:'Synthetic login rejected'});throw Error('Synthetic rejected');}authenticated=true;
   const info={device_id:'CSP-W2-'+vector.device,firmware:'SYNTHETIC',...(capable?{owner_login:true}:{})};onState({state:'connected',message:'Authenticated',info});return info;}
  disconnect(){authenticated=false;onState?.({state:'disconnected',message:'Closed'});}
  async command(op,args={}){
   assert(authenticated,'Synthetic application request after auth closed');calls.push({op,args:{...args}});
   if(op==='list')return {ok:true,revision:'2',count:0,profiles:[],next:0};
   if(op==='status')return {ok:true,revision:'2',durable:true,store_fault:false,wifi:'idle',wifi_error:0,wifi_profile:0,selected_profile:0,applied_revision:'0',internet_verified:false,cloud_available:false,backups_paused:true};
   if(op==='abort')return {ok:true};
   if(op==='login_begin'){edit=received=0;return answer();}
   if(op==='login_stage'){assert.equal(args.edit,edit);assert.equal(args.offset,received);++edit;received+=args.data.length/2;return answer();}
   if(op==='login_commit'){++commits;if(delay===op)await new Promise(resolve=>release=resolve);return commitFault?{ok:false,error:15,store_fault:true}:answer({revision:'427',durable:true,reconnect_required:true});}
   if(op==='login_abort'){edit=received=0;return answer();}
   throw Error('Unexpected fixture operation '+op);
  }
 }
 class OwnerLoginClient extends ActualOwnerLoginClient {
  constructor(connection){super(connection,{recordFactory:async()=>{if(delay==='generation')await new Promise(resolve=>release=resolve);recordWorking=Uint8Array.from(Buffer.from(vector.record,'hex'));return recordWorking;}});}
 }
 vm.runInNewContext(readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,''),
  {ManagementConnection:Connection,WifiSetupClient,WifiInputError,CloudSetupClient,CloudInputError,validateCloudFields,OwnerLoginClient,OwnerLoginInputError,OwnerRecordError,validateOwnerLogin,
   document,location:{protocol},isSecureContext:true,navigator:{bluetooth:{requestDevice(){}}},TextEncoder,addEventListener:(name,fn)=>windowEvents.set(name,fn)});
 return {e,document,options,calls,event:name=>(windowEvents.get(name)||documentEvents.get(name))?.({persisted:true}),release:()=>release(),get waiting(){return !!release;},get commits(){return commits;},get working(){return recordWorking;},revoke:()=>onState({state:'error',message:'Synthetic owner revoked'})};
}
async function connect(f,{legacy=false}={}){
 if(legacy){f.e['owner-key'].value='PUBLIC_EXISTING_OWNER_KEY_ONLY_XX';f.e['owner-key'].oninput();}
 else{f.e['login-username'].value='saved-user';f.e['login-password'].value='PUBLIC_OLD_PASSWORD';}
 await f.e.connect.onclick();
}
async function review(f,{username=vector.username,password=vector.password,confirmation=password}={}){
 f.e['new-login-username'].value=username;f.e['new-login-password'].value=password;f.e['new-login-confirm'].value=confirmation;await f.e['owner-login-review'].onclick();
}
async function until(predicate){for(let i=0;i<80&&!predicate();++i)await Promise.resolve();assert(predicate());}
function cleared(f){
 assert(f.e['owner-login-card'].hidden);assert(f.e['owner-login-confirm-panel'].hidden);assert.equal(f.e['owner-login-accept'].checked,false);
 for(const id of ['login-username','login-password','new-login-username','new-login-password','new-login-confirm','owner-key'])assert.equal(f.e[id].value,'');
 assert.equal(f.e['owner-login-message'].textContent,'');assert.equal(f.e['identity'].textContent,'');assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');
}
test('normal sign-in passes username/password directly to Security2 connection and clears DOM input',async()=>{
 const f=fixture();assert.equal(f.e['login-username'].value,'');await connect(f);assert.equal(f.options[0].username,'saved-user');assert.equal(f.options[0].password,'PUBLIC_OLD_PASSWORD');assert(!Object.hasOwn(f.options[0],'ownerKey'));
 assert.equal(f.e['login-password'].value,'');assert(!f.e['owner-login-card'].hidden);assert.equal(f.e['new-login-username'].value,'admin');assert.equal(f.e['login-username'].value,'');
 assert(!f.calls.some(c=>c.op.startsWith('login_')));
});
test('existing key path keeps csp-owner semantics and never sends a replacement without consent',async()=>{
 const f=fixture();await connect(f,{legacy:true});assert.equal(f.options[0].ownerKey,'PUBLIC_EXISTING_OWNER_KEY_ONLY_XX');assert(!Object.hasOwn(f.options[0],'password'));
 assert.equal(f.e['owner-key'].value,'');assert.equal(f.commits,0);assert(!f.calls.some(c=>c.op.startsWith('login_')));
});
test('old firmware supports sign-in but keeps replacement form hidden and sends no login API',async()=>{
 const f=fixture({capable:false});await connect(f);assert(!f.e['wifi-card'].hidden);assert(f.e['owner-login-card'].hidden);
 await review(f);assert(!f.calls.some(c=>c.op.startsWith('login_')));
});
test('failed authentication never exposes change form or application requests',async()=>{
 const f=fixture({failAuthentication:true});await connect(f);cleared(f);assert.equal(f.calls.length,0);
});
test('review stages verifier only, locks other work, and save requires separate explicit acceptance',async()=>{
 const f=fixture();await connect(f);await review(f);assert(!f.e['owner-login-confirm-panel'].hidden);assert(f.e.scan.disabled);assert(f.e['cloud-review'].disabled);assert(f.e['new-login-password'].disabled);
 assert.equal(f.e['new-login-password'].value,'');assert.equal(f.e['new-login-confirm'].value,'');assert(f.working.every(value=>value===0));assert.equal(f.commits,0);
 assert(!JSON.stringify(f.calls).includes(vector.password));assert.deepEqual(f.calls.filter(c=>c.op==='login_stage').map(c=>c.args.offset),[0,128,256,384]);
 await f.e['owner-login-save'].onclick();assert.equal(f.commits,0);f.e['owner-login-accept'].checked=true;await f.e['owner-login-save'].onclick();
 assert.equal(f.commits,1);cleared(f);assert.match(f.e['owner-login-outcome'].textContent,/new login was saved/);assert.match(f.e['owner-login-outcome'].textContent,/previous login or private key has been replaced/);
});
test('cancel clears staging/passwords and preserves current owner credential until explicit future commit',async()=>{
 const f=fixture();await connect(f);await review(f);await f.e['owner-login-cancel'].onclick();assert.equal(f.commits,0);
 assert(f.e['owner-login-confirm-panel'].hidden);assert.equal(f.e['owner-login-accept'].checked,false);assert(!f.e.scan.disabled);assert.match(f.e['owner-login-message'].textContent,/No replacement login was committed/);
});
for(const [name,input] of [['short password',{password:'short'}],['mismatch',{confirmation:'PUBLIC_DIFFERENT_PASSWORD'}],['unsafe username',{username:'admin:other'}],['oversize Unicode',{password:'é'.repeat(65)}]])
 test(name+' never starts or commits a login transaction',async()=>{
  const f=fixture();await connect(f);await review(f,input);assert(!f.calls.some(c=>c.op==='login_begin'));assert.equal(f.commits,0);assert(f.e['owner-login-confirm-panel'].hidden);assert(f.e['owner-login-message'].textContent);
  assert.equal(f.e['new-login-password'].value,'');assert.equal(f.e['new-login-confirm'].value,'');
 });
for(const reason of ['disconnect','pagehide','pageshow','freeze','resume','visibilitychange','revoked'])test(reason+' clears all login form/authentication state',async()=>{
 const f=fixture();await connect(f);await review(f);f.e['login-password'].value='PUBLIC_LATE_PASSWORD';
 if(reason==='disconnect')f.e.disconnect.onclick();else if(reason==='revoked')f.revoke();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}
 cleared(f);assert.equal(f.commits,0);assert.equal(f.e['show-login-password'].checked,false);
});
test('late local verifier generation after freeze cannot send chunks or show staged login',async()=>{
 const f=fixture({delay:'generation'});await connect(f);f.e['new-login-username'].value=vector.username;f.e['new-login-password'].value=vector.password;f.e['new-login-confirm'].value=vector.password;
 const pending=f.e['owner-login-review'].onclick();await until(()=>f.waiting);f.event('freeze');f.release();await pending;cleared(f);
 assert(!f.calls.some(c=>c.op==='login_stage'));assert.equal(f.commits,0);assert(f.working.every(value=>value===0));
});
test('uncertain write closes authentication and instructs new-login/recovery, never retry',async()=>{
 const f=fixture({commitFault:true});await connect(f);await review(f);f.e['owner-login-accept'].checked=true;await f.e['owner-login-save'].onclick();cleared(f);
 assert.equal(f.commits,1);assert.match(f.e['owner-login-outcome'].textContent,/outcome is unknown/);assert.match(f.e['owner-login-outcome'].textContent,/new login/);assert.match(f.e['owner-login-outcome'].textContent,/Do not repeat Save automatically/);
});
test('disconnect during pending commit preserves unknown result; late success cannot resurrect a saved badge',async()=>{
 const f=fixture({delay:'login_commit'});await connect(f);await review(f);f.e['owner-login-accept'].checked=true;const pending=f.e['owner-login-save'].onclick();await until(()=>f.waiting);
 f.event('freeze');assert.match(f.e['owner-login-outcome'].textContent,/outcome is unknown/);f.release();await pending;cleared(f);assert.equal(f.commits,1);assert(!/was saved/.test(f.e['owner-login-outcome'].textContent));
});
test('normal password reveal is explicit and reset on disconnect; HTTP cannot collect credentials',async()=>{
 const f=fixture();f.e['show-login-password'].checked=true;f.e['show-login-password'].onchange();assert.equal(f.e['login-password'].type,'text');await connect(f);assert.equal(f.e['login-password'].type,'password');
 f.e.disconnect.onclick();cleared(f);const http=fixture({protocol:'http:'});http.e['login-username'].value='admin';http.e['login-password'].value='PUBLIC_PASSWORD';await http.e.connect.onclick();assert.equal(http.options.length,0);assert(http.e['login-password'].disabled);
});
test('form semantics allow password-manager choice without page storage, upload or native navigation',async()=>{
 assert.match(html,/id="login-username"[^>]*autocomplete="username"/);assert.match(html,/id="login-password"[^>]*autocomplete="current-password"/);
 assert.match(html,/id="new-login-password"[^>]*autocomplete="new-password"/);assert.match(html,/id="new-login-confirm"[^>]*autocomplete="new-password"/);
 assert.match(html,/browser may offer to remember/);assert.match(html,/form-action 'none'/);
 const app=readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8');assert(!/localStorage|sessionStorage|fetch\(|XMLHttpRequest|sendBeacon|console\./.test(app));
 const f=fixture();let prevented=0;await f.e['login-form'].onsubmit({preventDefault(){++prevented;}});assert.equal(prevented,1);
});
