import test from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {FirstOwnerSetup,ownerStatus,ownerIdentity} from '../frontend/first-owner.mjs';
import {createOwnerRecord} from '../frontend/shared/security2.mjs';
import {SetupWizardModel} from '../frontend/wizard-model.mjs';
import {DesktopBridge} from '../frontend/bridge.mjs';
import {mountSetupWizard} from '../frontend/wizard.mjs';
const device='001122334455',username='synthetic-owner',password='SYNTHETIC memorable private passphrase';
const missing={owner_state:1,boot_down:false,held_ms:0,blocked:16,can_enroll:false,last_blocked:0,install_step:0};
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
function fixture({initial={...missing},factoryGate,installGate,confirmGate,installUnknown=false,cleanupGate}={}){
 const calls=[],states=[],locks=[],control={status:initial,identity:device,record:null,auth:null,closes:0,drains:0,timer:null};
 const bridge={async request(op,args){calls.push(op);
  if(op==='usb_owner_status')return {...control.status};if(op==='usb_owner_identity')return {device:control.identity};
  if(op==='usb_owner_install'){assert.equal(args.port,'synthetic-port');assert.match(args.record_hex,/^[0-9a-f]{864}$/);if(installGate)await installGate.promise;if(installUnknown)throw Object.assign(Error('SYNTHETIC hidden detail'),{code:'owner_install_unknown'});control.status={...missing,owner_state:0,boot_down:true,held_ms:12000,blocked:1,install_step:6};return {installed:true};}
  if(op==='usb_open_setup')return {accepted:true};throw Error('Unexpected operation');
 }};
 const bluetooth={async drain(){++control.drains;calls.push('drain');if(cleanupGate&&control.drains>1)await cleanupGate.promise;},forDevice(id){control.exactId=id;return {requestDevice(){}};}};
 const setup=new FirstOwnerSetup({bridge,bluetooth,prepare(){calls.push('prepare');return true;},connect(auth){control.auth={...auth};control.authArgument=auth;calls.push('authenticate');return confirmGate?confirmGate.promise:Promise.resolve(true);},cancel(){++control.closes;},setPending:value=>locks.push(value),onState:value=>states.push(value),async recordFactory(args){if(factoryGate)await factoryGate.promise;control.record=await createOwnerRecord({...args,cryptoProvider:webcrypto});return control.record;},setTimer(fn){control.timer=fn;return 1;},clearTimer(){control.timer=null;}});
 const review=()=>setup.review('synthetic-port',{username,password,confirmation:password});
 const ready=async()=>{control.status={...missing,boot_down:true,held_ms:10001,blocked:0,can_enroll:true};return setup.readiness();};
 return {setup,control,calls,states,locks,review,ready};
}
test('exact owner state distinguishes Missing, Ready and Fault; malformed/coerced status is rejected',()=>{
 for(const value of [missing,{...missing,owner_state:0},{...missing,owner_state:2}])assert.deepEqual(ownerStatus(value),value);
 for(const value of [{owner_ready:false},{...missing,owner_state:false},{...missing,owner_state:'1'},{...missing,extra:'untrusted'},{...missing,blocked:1024},{...missing,held_ms:-1},{...missing,blocked:0,can_enroll:false},{...missing,can_enroll:true},{...missing,owner_state:0,can_enroll:true,boot_down:true,held_ms:10000,blocked:0}])assert.throws(()=>ownerStatus(value));
 assert.equal(ownerIdentity({device}),device);for(const value of [{device:'00112233445F'},{device,extra:true},{owner_device:device}])assert.throws(()=>ownerIdentity(value));
});
test('owner_ready false alone never exposes first-account navigation; Fault is recovery only',()=>{
 const m=new SetupWizardModel();m.next();m.checkedUsb({ownerReady:false});assert.equal(m.startFirstOwner(),false);m.checkedOwner(2);assert.equal(m.startFirstOwner(),false);
 m.checkedOwner(0);assert.equal(m.ownerReady,true);assert.equal(m.startFirstOwner(),false);m.checkedOwner(1);assert.equal(m.ownerReady,false);assert.equal(m.startFirstOwner(),true);assert.equal(m.step,'first-owner');
});
test('Review uses actual shared Security2 record generator locally; no install/save is sent',async()=>{
 const f=fixture();assert.equal(await f.review(),true);assert.deepEqual(f.calls,['prepare','drain','usb_owner_status','usb_owner_identity']);assert.equal(f.setup.state().prepared,true);
 assert.equal(f.control.record.length,432);assert.deepEqual([...f.control.record.subarray(0,5)],[67,79,87,50,2]);assert.deepEqual([...f.control.record.subarray(6,12)],[0,17,34,51,68,85]);assert.notEqual(f.control.record[12],0);assert.notEqual(f.control.record[28],0);
 for(const state of f.states){assert.ok(Object.values(state).every(v=>typeof v==='boolean'));assert.doesNotMatch(JSON.stringify(state),/synthetic|password|record|001122/);}assert.equal(JSON.stringify(f.setup),'{}');await f.setup.cancel();assert.ok(f.control.record.every(b=>b===0));
});
test('Ready, Fault, unsupported transport or changed identity cannot be treated as blank',async()=>{
 for(const owner_state of [0,2]){const f=fixture({initial:{...missing,owner_state}});await assert.rejects(f.review());assert.equal(f.calls.includes('usb_owner_install'),false);assert.equal(f.control.record,null);}
});
test('fresh real readiness is required; 9999ms or blocked conditions never allow Create',async()=>{
 const f=fixture();await f.review();for(const status of [{...missing,boot_down:true,held_ms:9999},{...missing,boot_down:true,held_ms:10001,blocked:4}]){f.control.status=status;assert.equal(await f.setup.readiness(),false);assert.equal(f.setup.state().canEnroll,false);await assert.rejects(f.setup.install({accept:true}));}
 assert.equal(await f.ready(),true);await assert.rejects(f.setup.install({accept:false}));assert.equal(f.calls.includes('usb_owner_install'),false);await f.setup.cancel();
});
test('explicit Create writes exactly once; conclusive ACK requires release and fresh chosen-login proof',async()=>{
 const gate=deferred(),f=fixture({confirmGate:gate});await f.review();await f.ready();assert.equal(await f.setup.install({accept:true}),true);assert.ok(f.control.record.every(b=>b===0));assert.equal(f.setup.state().installed,true);assert.equal(f.setup.state().confirmed,false);
 assert.equal(await f.setup.confirm(),false);assert.equal(f.calls.includes('authenticate'),false,'Do not pretend BOOT was released');
 f.control.status={...f.control.status,boot_down:false,held_ms:0};const pending=f.setup.confirm();await tick();assert.equal(f.setup.state().confirmed,false);assert.equal(f.control.auth.username,username);assert.equal(f.control.auth.password,password);assert.equal(f.control.auth.expectedDevice,device);assert.equal(f.control.exactId,'00:11:22:33:44:55');assert.equal(Object.hasOwn(f.control.auth,'ownerKey'),false);assert.equal(f.control.authArgument.password,'');assert.equal(f.control.authArgument.username,'');
 gate.resolve(true);assert.equal(await pending,true);assert.equal(f.setup.state().confirmed,true);assert.equal(f.calls.filter(x=>x==='usb_owner_install').length,1);assert.equal(f.locks.at(-1),false);
});
test('unknown install ACK latches no-repeat and clears credentials/record without claiming success',async()=>{
 const f=fixture({installUnknown:true});await f.review();await f.ready();await assert.rejects(f.setup.install({accept:true}),e=>e.code==='owner_install_unknown');assert.equal(f.setup.state().unknown,true);assert.equal(f.setup.state().installed,false);assert.equal(f.setup.state().prepared,false);assert.ok(f.control.record.every(b=>b===0));
 await assert.rejects(f.setup.install({accept:true}));await assert.rejects(f.review());assert.equal(f.calls.filter(x=>x==='usb_owner_install').length,1);
 const m=new SetupWizardModel();m.updateFirstOwner(f.setup.state());assert.equal(m.step,'auth');assert.equal(m.notice,'first-owner-unknown');assert.equal(m.startFirstOwner(),false);
});
test('cancel during potential write preserves unknown outcome and cannot retry on late ACK',async()=>{
 const gate=deferred(),f=fixture({installGate:gate});await f.review();await f.ready();const pending=f.setup.install({accept:true});await tick();f.setup.cancel();assert.equal(f.setup.state().unknown,true);assert.equal(f.locks.at(-1),true);gate.resolve();await assert.rejects(pending);assert.equal(f.setup.state().confirmed,false);assert.equal(f.setup.state().prepared,false);await assert.rejects(f.setup.install({accept:true}));assert.equal(f.calls.filter(x=>x==='usb_owner_install').length,1);
});
test('cancelled crypto or native result cannot resurrect a record or release cleanup ownership early',async()=>{
 const gate=deferred(),cleanup=deferred(),f=fixture({factoryGate:gate,cleanupGate:cleanup});const pending=f.review();await tick();f.setup.cancel();gate.resolve();await tick();assert.equal(f.locks.at(-1),true);assert.equal(f.setup.running,true);cleanup.resolve();await assert.rejects(pending);assert.equal(f.setup.state().prepared,false);assert.equal(f.calls.includes('usb_owner_install'),false);assert.equal(f.locks.at(-1),false);
});
test('candidate expires after 120s without saving; installed-but-unverified expiry requires sign-in',async()=>{
 for(const installed of [false,true]){const f=fixture();await f.review();if(installed){await f.ready();await f.setup.install({accept:true});}const expire=f.control.timer;assert.equal(typeof expire,'function');expire();await tick();assert.equal(f.setup.state().expired,true);assert.equal(f.setup.state().prepared,false);assert.ok(f.control.record.every(b=>b===0));assert.equal(f.calls.filter(x=>x==='usb_owner_install').length,installed?1:0);}
});
test('postinstall full identity mismatch fails before authentication, never overwrites the owner',async()=>{
 const f=fixture();await f.review();await f.ready();await f.setup.install({accept:true});f.control.status.boot_down=false;f.control.identity='aabbcc334455';await assert.rejects(f.setup.confirm(),e=>e.code==='owner_confirmation_needed');assert.equal(f.calls.includes('authenticate'),false);assert.equal(f.calls.filter(x=>x==='usb_owner_install').length,1);assert.equal(f.setup.state().prepared,false);
});
test('native bridge first-owner requests are strictly allowlisted and contain no arbitrary path',async()=>{
 const callbacks=new Set(),requests=[],native={request(raw){requests.push(JSON.parse(raw));},response:{connect:f=>callbacks.add(f),disconnect:f=>callbacks.delete(f)},events:{connect(){},disconnect(){}}},bridge=new DesktopBridge(native);
 for(const [op,args]of [['usb_owner_status',{port:'synthetic-port'}],['usb_owner_identity',{port:'synthetic-port'}],['usb_owner_install',{port:'synthetic-port',record_hex:'00'.repeat(432)}]]){const p=bridge.request(op,args);for(const cb of callbacks)cb(JSON.stringify({id:requests.at(-1).id,ok:true,result:{}}));await p;}
 for(const args of [{port:'synthetic-port',record_hex:'00'.repeat(431)},{port:'synthetic-port',record_hex:'AA'.repeat(432)},{port:'synthetic-port',record_hex:'00'.repeat(432),password},{port:'synthetic-port',record_hex:'00'.repeat(432),path:'/not/allowed'}])await assert.rejects(bridge.request('usb_owner_install',args));assert.equal(requests.length,3);bridge.close();
});
function uiFixture(){
 class Element{
  constructor(id=''){this.id=id;this.value='';this.hidden=false;this.checked=false;this.disabled=false;this.textContent='';this.children=[];this.listeners=new Map();this.dataset={};this.attributes={};this.classList={toggle(){}};}
  append(...nodes){this.children.push(...nodes);}setAttribute(k,v){this.attributes[k]=v;}removeAttribute(k){delete this.attributes[k];}toggleAttribute(k,v){if(v)this.attributes[k]='';else delete this.attributes[k];}focus(){}
  addEventListener(type,callback){if(!this.listeners.has(type))this.listeners.set(type,[]);this.listeners.get(type).push(callback);}
  click(){if(!this.disabled)for(const callback of this.listeners.get('click')??[])callback({preventDefault(){},stopImmediatePropagation(){}});}
 }
 const nodes=new Map(),get=id=>{if(!nodes.has(id))nodes.set(id,new Element(id));return nodes.get(id);},panels=['welcome','usb','auth','login','first-owner','wifi','cloud','finish','records'].map(step=>{const e=new Element();e.dataset.wizardStep=step;return e;}),handlers=new Map();
 const document={getElementById:get,createElement:()=>new Element(),querySelector:()=>panels.find(p=>p.dataset.wizardStep==='cloud'),querySelectorAll:query=>query==='[data-wizard-step]'?panels:[],addEventListener:(type,callback)=>handlers.set(type,callback)};
 const old={MutationObserver:globalThis.MutationObserver,addEventListener:globalThis.addEventListener,scrollTo:globalThis.scrollTo};globalThis.MutationObserver=class{observe(){}};globalThis.addEventListener=()=>{};globalThis.scrollTo=()=>{};
 const toolbar={ports:new Element(),buttons:{refresh:new Element(),status:new Element(),open:new Element()}};toolbar.ports.value='synthetic-port';toolbar.ports.options=[{},{}];
 const calls=[],context={getSetupState:()=>({})},wizard=mountSetupWizard({document,context,toolbar});wizard.model.step='usb';wizard.model.checkedUsb({ownerReady:false});wizard.model.checkedOwner(1);
 const input=(pass=password,confirm=pass)=>{get('first-owner-username').value=username;get('first-owner-password').value=pass;get('first-owner-confirm').value=confirm;handlers.get('input')({target:get('first-owner-password')});};
 return {get,wizard,input,context,calls,restore(){for(const[k,v]of Object.entries(old))if(v===undefined)delete globalThis[k];else globalThis[k]=v;}};
}
test('actual first-account UI validates before Review and Enter never creates an account',()=>{
 const f=uiFixture();try{
  f.wizard.refresh();assert.equal(f.get('wizard-next').textContent,'Create my first account');f.get('wizard-next').click();assert.equal(f.wizard.model.step,'first-owner');f.input('admin');assert.equal(f.get('wizard-next').disabled,true);assert.match(f.get('first-owner-validation').textContent,/too short/);
  let prevented=false;f.get('first-owner-form').onsubmit({preventDefault(){prevented=true;}});assert.equal(prevented,true);assert.equal(f.calls.length,0);
 }finally{f.restore();}
});
test('actual first-account Review clears input references and Create keeps an explicit confirmation barrier',async()=>{
 const f=uiFixture();try{
  const gate=deferred();let supplied;f.context.reviewFirstOwner=args=>{supplied=args;assert.equal(args.username,username);assert.equal(args.password,password);f.calls.push('review');return gate.promise;};f.wizard.model.startFirstOwner();f.input();f.get('wizard-next').click();
  assert.equal(f.get('first-owner-password').value,'');assert.equal(f.get('first-owner-confirm').value,'');assert.equal(supplied.password,'');assert.equal(supplied.username,'');assert.deepEqual(f.calls,['review']);
  f.wizard.firstOwnerState({prepared:true});gate.resolve(true);await tick();assert.equal(f.get('wizard-next').textContent,'Check BOOT readiness');assert.equal(f.get('first-owner-review').hidden,false);
  f.wizard.firstOwnerState({prepared:true,canEnroll:true});assert.equal(f.get('wizard-next').textContent,'Create account on this USB');assert.equal(f.get('wizard-next').disabled,true);
  f.get('first-owner-accept').checked=true;f.wizard.refresh();assert.equal(f.get('wizard-next').disabled,false);assert.deepEqual(f.calls,['review'],'No create command without the next explicit click');assert.equal(f.get('first-owner-password').attributes['aria-invalid'],undefined);
 }finally{f.restore();}
});
test('first-account UI unknown write directs chosen-login sign-in, never presents another Create',()=>{
 const f=uiFixture();try{f.wizard.model.startFirstOwner();f.wizard.firstOwnerState({unknown:true});assert.equal(f.wizard.model.step,'auth');assert.match(f.get('wizard-message').textContent,/Do not repeat Create/);assert.equal(f.wizard.model.startFirstOwner(),false);assert.equal(f.get('first-owner-password').value,'');}finally{f.restore();}
});
