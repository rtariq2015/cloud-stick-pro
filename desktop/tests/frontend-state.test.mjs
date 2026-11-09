import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
import {SetupStatePublisher,SETUP_BOOLEAN_FIELDS,desktopContext,publishSetupState} from '../frontend/runtime.mjs';
import {mountSettings} from '../frontend/settings.mjs';
import {DirectSetupConnector} from '../frontend/direct-setup.mjs';
const empty=()=>({...Object.fromEntries(SETUP_BOOLEAN_FIELDS.map(k=>[k,false])),authenticatedGeneration:0});
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}

test('published schema is exact, immutable, boolean-only and excludes identifying values',()=>{
 const events=[],p=new SetupStatePublisher(x=>events.push(x));
 const value=p.publish({...empty(),authenticated:true,authenticatedGeneration:1,ssid:'SYNTHETIC-NETWORK',password:'SYNTHETIC-SECRET',username:'SYNTHETIC-USER'});
 assert.deepEqual(Object.keys(value).sort(),[...SETUP_BOOLEAN_FIELDS,'authenticatedGeneration'].sort());assert.ok(Object.isFrozen(value));
 assert.doesNotMatch(JSON.stringify(value),/SYNTHETIC/);assert.equal(events.length,1);
 assert.throws(()=>p.publish({...value,busy:'secret'}));assert.throws(()=>p.publish({...value,authenticatedGeneration:0}));
});
test('disconnect clears all stale live facts before publication while retaining result markers',()=>{
 const events=[],p=new SetupStatePublisher(x=>events.push(x));
 p.publish({...empty(),...Object.fromEntries(SETUP_BOOLEAN_FIELDS.map(k=>[k,true])),authenticatedGeneration:3});
 const value=p.publish({...p.get(),authenticated:false,busy:false});
 for(const k of ['loginSupported','loginStaged','cloudStaged','portalPrepared','wifiConfigured','wifiCandidatePending','wifiSavedThisSession','cloudConfigured','cloudSavedThisSession'])assert.equal(value[k],false,k);
 for(const k of ['loginSaved','wifiSaveUnknown','cloudSaveUnknown','outcomeUnknown'])assert.equal(value[k],true,k);
 assert.equal(events.at(-1),value);assert.equal(value.authenticatedGeneration,3);
});
test('unknown Wi-Fi/cloud saves cannot be published as a known outcome',()=>{
 const p=new SetupStatePublisher();assert.equal(p.publish({...empty(),wifiSaveUnknown:true}).outcomeUnknown,true);
 assert.equal(p.publish({...empty(),cloudSaveUnknown:true}).outcomeUnknown,true);
});
test('runtime dispatches the named CustomEvent with the exact sanitized getter snapshot',()=>{
 const before=globalThis.dispatchEvent,oldEvent=globalThis.CustomEvent,events=[];
 if(!globalThis.CustomEvent)globalThis.CustomEvent=class {constructor(type,{detail}){this.type=type;this.detail=detail;}};
 globalThis.dispatchEvent=event=>{events.push(event);return true;};
 try{publishSetupState({...empty(),busy:true,secret:'SYNTHETIC'});assert.equal(events.length,1);assert.equal(events[0].type,'cloudstick:setup-state');assert.equal(events[0].detail,desktopContext.getSetupState());assert.ok(Object.isFrozen(events[0].detail));assert.doesNotMatch(JSON.stringify(events[0].detail),/SYNTHETIC|secret/);}
 finally{if(before===undefined)delete globalThis.dispatchEvent;else globalThis.dispatchEvent=before;if(oldEvent===undefined)delete globalThis.CustomEvent;else globalThis.CustomEvent=oldEvent;}
});

async function fixture(options={}){
 const source=(await readFile(new URL('../frontend/app.mjs',import.meta.url),'utf8')).replace(/^import .*;\n/gm,'');
 class Element {constructor(){this.value='';this.checked=false;this.hidden=true;this.disabled=false;this.textContent='';this.children=[];this.files=[];}replaceChildren(...x){this.children=x;}append(...x){this.children.push(...x);}addEventListener(){} }
 const nodes=new Map(),get=id=>{if(!nodes.has(id))nodes.set(id,new Element());return nodes.get(id);};
 const events=[],globalEvents=new Map(),documentEvents=new Map(),state=new SetupStatePublisher(x=>events.push(x));
 const control={...options,connections:[],saved:{durable:true,store_fault:false,revision:'7',selected_profile:1,wifi:'local_connected',wifi_profile:1,applied_revision:'7'},rows:[{id:1,enabled:true,ssid:'SYNTHETIC-NETWORK',open:false,priority:0,has_secret:true}],cloud:{ok:true,revision:'9',configured:true,provider:'koofr',has_username:true,has_password:true,has_folder:true,editing:false,edit:0,store_fault:false}};
 class WifiInputError extends Error {constructor(code){super('synthetic');this.code=code;}}
 class CloudInputError extends Error {constructor(code,{uncertain=false}={}){super('synthetic');this.code=code;this.uncertain=uncertain;}}
 class OwnerLoginInputError extends Error {constructor(code,{uncertain=false}={}){super('synthetic');this.code=code;this.uncertain=uncertain;}}
 class OwnerRecordError extends Error {}
 class ManagementConnection {
  constructor(opts){this.opts=opts;control.connections.push(this);}
  async connect(){this.opts.onState({state:'connected',message:'Synthetic authenticated link'});return {device_id:'CSP-W2-001122334455',firmware:'SYNTHETIC',owner_login:true,cloud_setup:true,public_wifi:control.noPortal!==true,backup_status:control.records===true,event_log:control.records===true,file_history:control.records===true};}
  disconnect(){this.opts.onState({state:'disconnected',message:'Synthetic disconnect'});}
 }
 class WifiSetupClient {
  async profiles(){control.profileReads=(control.profileReads??0)+1;if(control.profilesGate)await control.profilesGate.promise;return control.rows;}
  async status(){return control.saved;}
  async backupStatus(){control.backupReads=(control.backupReads??0)+1;return {cloud_configured:true,last_backup_unix:1793868458,verified:10,pending:0,failed:0,backup_error:0,backups_paused:true};}
  async abort(){}
  async stage(){}
  async check(){}
  async save(){if(control.wifiSaveGate)await control.wifiSaveGate.promise;control.saved={...control.saved,revision:control.wifiMismatch?'wrong':'8',applied_revision:'8'};return {durable:true,revision:'8'};}
 }
 class CloudSetupClient {
  async status(){if(control.failCloudInit)throw Error('synthetic init failure');if(control.cloudStatusGate)await control.cloudStatusGate.promise;return control.cloud;}
  async stage(){}
  async abort(){}
  async save(){if(control.cloudSaveGate)await control.cloudSaveGate.promise;control.cloud={...control.cloud,revision:'10'};return {durable:true,revision:'10'};}
 }
 class OwnerLoginClient {
  async stage(){}
  async abort(){}
  async save(){if(control.loginSaveGate)await control.loginSaveGate.promise;if(control.loginReject)throw new OwnerLoginInputError(6);return {durable:true,reconnect_required:true};}
 }
 class PublicWifiClient {async prepare(){return {handoff_ms:30000};}async cancel(){} }
 const desktopContext={secureContext:true,bluetooth:{requestDevice(){}},closeTransport:null,getSetupState:()=>state.get()};
 class DiagnosticLogClient {async read(){control.activityReads=(control.activityReads??0)+1;return {events:[],loaded:true,persisted:0};}}
 class FileHistoryClient {async read(){control.historyReads=(control.historyReads??0)+1;if(control.recordGate)await control.recordGate.promise;if(control.recordFail)throw Error('SYNTHETIC private history error');return {loaded:true,when:'1793868458',rows:[],matched:0,complete:true,limited:false,corrupt:false,io_failed:false,allocation_failed:false,exhausted:false};}}
 const context=vm.createContext({mountSettings,console,TextEncoder,TextDecoder,Uint8Array,Date,setTimeout,clearTimeout,desktopContext,publishSetupState:x=>state.publish(x),ManagementConnection,WifiSetupClient,CloudSetupClient,OwnerLoginClient,PublicWifiClient,WifiInputError,CloudInputError,OwnerLoginInputError,OwnerRecordError,validateOwnerLogin(){},validateCloudFields(){},DiagnosticLogClient,FileHistoryClient,addEventListener:(type,fn)=>globalEvents.set(type,fn),document:{visibilityState:'visible',getElementById:get,createElement:()=>new Element(),createTextNode:x=>x,addEventListener:(type,fn)=>documentEvents.set(type,fn)}});
 new vm.Script(source,{filename:'actual-desktop-app.mjs'}).runInContext(context);
 const connect=async()=>{get('login-username').value='synthetic-owner';get('login-password').value='SYNTHETIC-PASSWORD';return get('connect').onclick();};
 const stageLogin=async()=>{get('new-login-username').value='synthetic-new-owner';get('new-login-password').value=get('new-login-confirm').value='SYNTHETIC-NEW-PASSWORD';await get('owner-login-review').onclick();get('owner-login-accept').checked=true;};
 const chooseWifi=async()=>{get('manual-ssid').value='SYNTHETIC-NETWORK';await get('manual').onclick();get('wifi-password').value='SYNTHETIC-WIFI';await get('check').onclick();get('accept-local').checked=true;};
 const stageCloud=async()=>{get('cloud-keep-login').checked=false;get('cloud-keep-folder').checked=false;get('cloud-email').value='synthetic@example.invalid';get('cloud-password').value='SYNTHETIC-CLOUD';get('cloud-folder').value='Synthetic';await get('cloud-review').onclick();get('cloud-accept').checked=true;};
 return {get,control,events,state,desktopContext,connect,stageLogin,chooseWifi,stageCloud,globalEvents,documentEvents};
}
test('actual app does not authenticate wizard on raw GATT/handshake or failed initialization',async()=>{
 const gate=deferred(),f=await fixture({profilesGate:gate});const connecting=f.connect();await tick();
 assert.equal(f.state.get().authenticated,false);assert.equal(f.state.get().busy,true);assert.equal(f.state.get().authenticatedGeneration,0);
 gate.resolve();await connecting;assert.equal(f.state.get().authenticated,true);assert.equal(f.state.get().authenticatedGeneration,1);assert.equal(f.state.get().wifiConfigured,true);assert.equal(f.state.get().cloudConfigured,true);
 const failed=await fixture({failCloudInit:true});await failed.connect();assert.equal(failed.state.get().authenticated,false);assert.equal(failed.state.get().authenticatedGeneration,0);
});
test('optional private history is deferred until requested and loaded once per authenticated session',async()=>{
 const f=await fixture({records:true});await f.connect();assert.equal(f.state.get().authenticated,true);assert.equal(f.control.backupReads,1);assert.equal(f.control.activityReads??0,0);assert.equal(f.control.historyReads??0,0);
 assert.equal(await f.desktopContext.readRecordedStatus(),true);assert.equal(f.control.backupReads,2);assert.equal(f.control.activityReads,1);assert.equal(f.control.historyReads,1);assert.equal(await f.desktopContext.readRecordedStatus(),false);assert.equal(f.control.historyReads,1);
 await f.get('records-refresh').onclick();assert.equal(f.control.historyReads,2);assert.match(f.get('records-read-at').textContent,/recorded earlier/);assert.equal(f.get('backup-verified').textContent,'10');
});
test('unsupported record capabilities never pretend a snapshot was loaded',async()=>{
 const f=await fixture();await f.connect();assert.equal(await f.desktopContext.readRecordedStatus(),false);assert.equal(f.control.historyReads??0,0);assert.equal(f.get('backup-card').hidden,true);assert.match(f.get('records-message').textContent,/does not offer.*No snapshot was read/);
});
test('deferred records retain busy admission and do not repaint after a cancelled generation',async()=>{
 const gate=deferred(),f=await fixture({records:true,recordGate:gate});await f.connect();const pending=f.desktopContext.readRecordedStatus();await tick();assert.equal(f.state.get().busy,true);assert.equal(f.get('records-refresh').disabled,true);assert.equal(await f.desktopContext.readRecordedStatus(true),false);assert.equal(f.control.historyReads,1);
 f.get('disconnect').onclick();gate.resolve();await pending;assert.equal(f.get('backup-card').hidden,true);assert.equal(f.get('file-history-card').hidden,true);assert.equal(f.get('records-read-at').textContent,'');assert.equal(f.state.get().authenticated,false);
});
test('failed optional history never retries through repeated calls or exposes partial new counts',async()=>{
 const f=await fixture({records:true,recordFail:true});await f.connect();assert.equal(await f.desktopContext.readRecordedStatus(),false);assert.equal(f.get('backup-card').hidden,true);assert.equal(f.get('backup-verified').textContent,'');assert.equal(await f.desktopContext.readRecordedStatus(),false);assert.equal(f.control.historyReads,1);assert.match(f.get('records-message').textContent,/No fresh results.*no automatic retry/);assert.doesNotMatch(f.get('records-message').textContent,/SYNTHETIC/);
});
test('a confirmed cloud revision change invalidates prior record scope before another explicit view',async()=>{
 const f=await fixture({records:true});await f.connect();await f.desktopContext.readRecordedStatus();assert.equal(f.get('backup-card').hidden,false);await f.stageCloud();await f.get('cloud-save').onclick();assert.equal(f.get('backup-card').hidden,true);assert.equal(f.get('backup-verified').textContent,'');assert.equal(await f.desktopContext.readRecordedStatus(),true);assert.equal(f.control.historyReads,2);
});
test('actual direct access locks inputs, skips legacy fields and waits for complete initialization',async()=>{
 const gate=deferred(),f=await fixture({profilesGate:gate}),provider={requestDevice(){}},key='0123456789abcdef0123456789abcdef';
 assert.equal(f.desktopContext.prepareSetupAccess(),true);assert.equal(f.state.get().busy,true);assert.equal(f.get('connect').disabled,true);
 const pending=f.desktopContext.connectSetupAccess({ownerKey:key,bluetooth:provider,expectedDevice:'001122334455'});await tick();
 assert.equal(f.state.get().authenticated,false);assert.equal(f.control.connections[0].opts.bluetooth,provider);assert.equal(f.control.connections[0].opts.ownerKey,key);assert.equal(f.control.connections[0].opts.sessionTimeoutMs,null);
 assert.equal(f.get('owner-key').value,'');assert.equal(f.get('login-password').value,'');assert.doesNotMatch(JSON.stringify(f.events),/0123456789abcdef|synthetic-owner|password/);
 gate.resolve();assert.equal(await pending,true);assert.equal(f.state.get().authenticated,true);assert.equal(f.state.get().authenticatedGeneration,1);
});
test('actual direct authentication binds the full USB-derived identity before any settings read',async()=>{
 const f=await fixture();assert.equal(f.desktopContext.prepareSetupAccess(),true);
 assert.notEqual(await f.desktopContext.connectSetupAccess({ownerKey:'0123456789abcdef0123456789abcdef',bluetooth:{requestDevice(){}},expectedDevice:'aabbcc334455'}),true);
 assert.equal(f.control.profileReads??0,0);assert.equal(f.state.get().authenticated,false);assert.equal(f.state.get().authenticatedGeneration,0);
});
test('actual first-owner proof uses the chosen username/password and full identity before initialization',async()=>{
 const f=await fixture();f.desktopContext.setSetupAccessPending(true);assert.equal(f.desktopContext.prepareSetupAccess(),true);
 assert.equal(await f.desktopContext.connectFirstOwner({username:'synthetic-new-owner',password:'SYNTHETIC-CHOSEN-PHRASE',bluetooth:{requestDevice(){}},expectedDevice:'001122334455'}),true);
 f.desktopContext.setSetupAccessPending(false);const options=f.control.connections[0].opts;assert.equal(options.username,'synthetic-new-owner');assert.equal(options.password,'SYNTHETIC-CHOSEN-PHRASE');assert.equal(Object.hasOwn(options,'ownerKey'),false);assert.equal(f.state.get().authenticated,true);assert.doesNotMatch(JSON.stringify(f.events),/SYNTHETIC|synthetic-new-owner|password/);assert.equal(f.get('login-password').value,'');assert.equal(f.get('owner-key').value,'');
 const wrong=await fixture();wrong.desktopContext.prepareSetupAccess();assert.notEqual(await wrong.desktopContext.connectFirstOwner({username:'synthetic-new-owner',password:'SYNTHETIC-CHOSEN-PHRASE',bluetooth:{requestDevice(){}},expectedDevice:'aabbcc334455'}),true);assert.equal(wrong.control.profileReads??0,0);assert.equal(wrong.state.get().authenticated,false);
});
test('actual direct access cancelled during initialization cannot publish late authentication',async()=>{
 const gate=deferred(),f=await fixture({profilesGate:gate});f.desktopContext.prepareSetupAccess();const pending=f.desktopContext.connectSetupAccess({ownerKey:'0123456789abcdef0123456789abcdef',bluetooth:{requestDevice(){}},expectedDevice:'001122334455'});await tick();
 f.globalEvents.get('pagehide')();gate.resolve();assert.notEqual(await pending,true);assert.equal(f.state.get().authenticated,false);assert.equal(f.state.get().authenticatedGeneration,0);
});
test('cancelled direct/native cleanup blocks manual and implicit submit until its old finally settles',async()=>{
 const f=await fixture(),accessGate=deferred(),cleanupGate=deferred();let drains=0;
 const direct=new DirectSetupConnector({bridge:{async request(op){assert.equal(op,'usb_setup_access');await accessGate.promise;return {key:'0123456789abcdef0123456789abcdef',device_id:'00:11:22:33:44:55'};}},bluetooth:{drain:()=>++drains===1?Promise.resolve():cleanupGate.promise,forDevice(){throw Error('Cancelled access must not select a device');}},prepare:f.desktopContext.prepareSetupAccess,connect:f.desktopContext.connectSetupAccess,cancel:f.desktopContext.cancelSetupAccess,setPending:f.desktopContext.setSetupAccessPending});
 const pending=direct.start('synthetic-port');await tick();direct.cancel();
 assert.equal(f.state.get().busy,true);assert.equal(f.get('connect').disabled,true);assert.equal(f.get('login-password').disabled,true);
 f.get('login-username').value='synthetic-owner';f.get('login-password').value='SYNTHETIC-PASSWORD';assert.equal(await f.get('connect').onclick(),false);assert.equal(f.control.connections.length,0);assert.equal(f.get('login-password').value,'SYNTHETIC-PASSWORD');
 accessGate.resolve();await tick();assert.equal(drains,2);assert.equal(f.state.get().busy,true);assert.equal(await f.get('connect').onclick(),false);assert.equal(f.control.connections.length,0);
 cleanupGate.resolve();await assert.rejects(pending);assert.equal(f.state.get().busy,false);assert.equal(f.get('connect').disabled,false);
 await f.connect();await tick();assert.equal(f.control.connections.length,1);assert.equal(f.state.get().authenticated,true);assert.equal(f.state.get().authenticatedGeneration,1);
});
test('actual login replacement disconnect message asks for the new login, not the old key',async()=>{
 const f=await fixture();await f.connect();await f.stageLogin();await f.get('owner-login-save').onclick();assert.match(f.get('connection-message').textContent,/new username and password/);assert.doesNotMatch(f.get('connection-message').textContent,/same private owner key/);
});
test('actual close/pagehide/freeze clear stale live state without publishing private names',async()=>{
 for(const mode of ['close','pagehide','freeze']){
  const f=await fixture();await f.connect();await f.chooseWifi();assert.equal(f.state.get().wifiCandidatePending,true);
  if(mode==='close')f.desktopContext.closeSetup();else if(mode==='pagehide')f.globalEvents.get(mode)();else f.documentEvents.get(mode)();
  const state=f.state.get();assert.equal(state.authenticated,false);assert.equal(state.loginSupported,false);assert.equal(state.wifiConfigured,false);assert.equal(state.cloudConfigured,false);assert.equal(state.wifiCandidatePending,false);
  assert.doesNotMatch(JSON.stringify(f.events),/SYNTHETIC|synthetic-owner|ssid|password|revision/);
 }
});
test('actual configuration flag requires a durable healthy enabled selected profile',async()=>{
 for(const change of [f=>f.control.rows[0].enabled=false,f=>f.control.saved.store_fault=true,f=>f.control.saved.durable=false,f=>f.control.saved.selected_profile=999]){
  const f=await fixture();change(f);await f.connect();assert.equal(f.state.get().authenticated,true);assert.equal(f.state.get().wifiConfigured,false);
 }
 const f=await fixture();await f.connect();await f.chooseWifi();assert.equal(f.state.get().wifiCandidatePending,true);f.get('cancel').onclick();await tick();assert.equal(f.state.get().wifiCandidatePending,false);assert.equal(f.state.get().wifiSavedThisSession,false);
});
test('actual successful login save retains outcome through deliberate close and resets on next connect',async()=>{
 const f=await fixture();await f.connect();await f.stageLogin();assert.equal(f.state.get().loginStaged,true);await f.get('owner-login-save').onclick();
 assert.equal(f.state.get().loginSaved,true);assert.equal(f.state.get().authenticated,false);assert.equal(f.state.get().outcomeUnknown,false);assert.equal(f.state.get().authenticatedGeneration,1);
 await f.connect();assert.equal(f.state.get().loginSaved,false);assert.equal(f.state.get().authenticated,true);assert.equal(f.state.get().authenticatedGeneration,2);
});
test('actual lost login-save ACK remains unknown; a definite rejection is not success or unknown',async()=>{
 const gate=deferred(),f=await fixture({loginSaveGate:gate});await f.connect();await f.stageLogin();const saving=f.get('owner-login-save').onclick();await tick();f.control.connections[0].disconnect();gate.resolve();await saving;
 assert.equal(f.state.get().outcomeUnknown,true);assert.equal(f.state.get().loginSaved,false);assert.equal(f.state.get().authenticated,false);
 await f.connect();assert.equal(f.state.get().outcomeUnknown,false);
 const rejected=await fixture({loginReject:true});await rejected.connect();await rejected.stageLogin();await rejected.get('owner-login-save').onclick();assert.equal(rejected.state.get().loginSaved,false);assert.equal(rejected.state.get().outcomeUnknown,false);
});
test('actual Wi-Fi save marker requires exact ACK/readback; candidate edits invalidate it',async()=>{
 const f=await fixture();await f.connect();await f.chooseWifi();assert.equal(f.state.get().wifiSavedThisSession,false);await f.get('save').onclick();
 assert.equal(f.state.get().wifiSavedThisSession,true);assert.equal(f.state.get().wifiCandidatePending,false);assert.equal(f.state.get().wifiSaveUnknown,false);
 f.get('manual-ssid').value='SYNTHETIC-EDIT';f.get('manual-ssid').oninput();assert.equal(f.state.get().wifiCandidatePending,true);assert.equal(f.state.get().wifiSavedThisSession,false);
 for(const options of [{wifiMismatch:true},{noPortal:true}]){const g=await fixture(options);await g.connect();await g.chooseWifi();await g.get('save').onclick();assert.equal(g.state.get().wifiSavedThisSession,false);assert.equal(g.state.get().wifiSaveUnknown,true);assert.equal(g.state.get().wifiCandidatePending,true);}
});
test('actual Wi-Fi lost ACK and cloud lost readback block completion until reconnect',async()=>{
 const wifiGate=deferred(),f=await fixture({wifiSaveGate:wifiGate});await f.connect();await f.chooseWifi();const wifiSave=f.get('save').onclick();await tick();f.control.connections[0].disconnect();wifiGate.resolve();await wifiSave;assert.equal(f.state.get().wifiSaveUnknown,true);assert.equal(f.state.get().outcomeUnknown,true);
 const g=await fixture();await g.connect();await g.stageCloud();const statusGate=deferred();g.control.cloudStatusGate=statusGate;const cloudSave=g.get('cloud-save').onclick();await tick();g.control.connections[0].disconnect();statusGate.resolve();await cloudSave;
 assert.equal(g.state.get().cloudSaveUnknown,true);assert.equal(g.state.get().cloudSavedThisSession,false);assert.equal(g.state.get().outcomeUnknown,true);
 g.control.cloudStatusGate=null;await g.connect();assert.equal(g.state.get().outcomeUnknown,false);
});
test('actual cloud explicit save is marked only after matching configured revision readback',async()=>{
 const f=await fixture();await f.connect();assert.equal(f.state.get().cloudConfigured,true);assert.equal(f.state.get().cloudSavedThisSession,false);await f.stageCloud();assert.equal(f.state.get().cloudStaged,true);await f.get('cloud-save').onclick();assert.equal(f.state.get().cloudSavedThisSession,true);assert.equal(f.state.get().cloudStaged,false);assert.equal(f.state.get().outcomeUnknown,false);
});
test('an unknown Wi-Fi outcome stays latched until a new authenticated session',async()=>{
 const f=await fixture({wifiMismatch:true});await f.connect();await f.chooseWifi();await f.get('save').onclick();assert.equal(f.state.get().wifiSaveUnknown,true);
 f.control.wifiMismatch=false;await f.chooseWifi();await f.get('save').onclick();assert.equal(f.state.get().wifiSaveUnknown,true);assert.equal(f.state.get().wifiSavedThisSession,false);
 f.desktopContext.closeSetup();await f.connect();assert.equal(f.state.get().wifiSaveUnknown,false);assert.equal(f.state.get().outcomeUnknown,false);
});
test('personal sign-in uses the USB-bound Bluetooth target and rejects a different full identity before reading settings',async()=>{
 const f=await fixture(),provider={requestDevice(){}};let prepared=0;
 f.desktopContext.preparePersonalBluetooth=async()=>{prepared++;return {bluetooth:provider,expectedDevice:'001122334455'};};await f.connect();assert.equal(prepared,1);assert.equal(f.control.connections[0].opts.bluetooth,provider);assert.equal(f.state.get().authenticated,true);
 const g=await fixture();g.desktopContext.preparePersonalBluetooth=async()=>({bluetooth:provider,expectedDevice:'aabbcc334455'});await g.connect();assert.equal(g.state.get().authenticated,false);assert.equal(g.control.profileReads??0,0);
});
test('a cancelled USB Bluetooth-open response cannot start a late personal authentication',async()=>{
 const gate=deferred(),f=await fixture();f.desktopContext.preparePersonalBluetooth=()=>gate.promise;const pending=f.connect();await tick();f.desktopContext.closeSetup();gate.resolve({bluetooth:{requestDevice(){}},expectedDevice:'001122334455'});await pending;assert.equal(f.control.connections.length,0);assert.equal(f.state.get().authenticated,false);
});
