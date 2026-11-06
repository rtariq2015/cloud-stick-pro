import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {mountSetupWizard,newLoginFeedback} from '../frontend/wizard.mjs';
import {validateOwnerLogin} from '../frontend/shared/security2.mjs';

const phrase='SYNTHETIC private phrase for testing';
function fixture({context={getSetupState:()=>({})}}={}){
 class Element{
  constructor(id=''){this.id=id;this.value='';this.textContent='';this.hidden=false;this.disabled=false;this.children=[];this.listeners=new Map();this.dataset={};this.attributes={};this.classList={toggle(){}};}
  append(...nodes){this.children.push(...nodes);}
  setAttribute(k,v){this.attributes[k]=v;}
  removeAttribute(k){delete this.attributes[k];}
  toggleAttribute(k,yes){if(yes)this.attributes[k]='';else delete this.attributes[k];}
  addEventListener(k,f,options={}){if(!this.listeners.has(k))this.listeners.set(k,[]);this.listeners.get(k).push({f,capture:options===true||options.capture===true});}
  focus(){}
  click(){
   if(this.disabled)return;
   const event={target:this,defaultPrevented:false,stopped:false,preventDefault(){this.defaultPrevented=true;},stopImmediatePropagation(){this.stopped=true;}};
   const listeners=this.listeners.get('click')??[];
   for(const row of listeners.filter(x=>x.capture)){row.f(event);if(event.stopped)return event;}
   this.onclick?.(event);if(event.stopped)return event;
   for(const row of listeners.filter(x=>!x.capture)){row.f(event);if(event.stopped)break;}
   return event;
  }
 }
 const ids=new Map(),get=id=>{if(!ids.has(id))ids.set(id,new Element(id));return ids.get(id);};
 const steps=['welcome','usb','auth','login','wifi','cloud','finish','records'],panels=steps.map(step=>{const n=new Element();n.dataset.wizardStep=step;return n;}),progress=steps.slice(1,7).map(step=>{const n=new Element();n.dataset.wizardProgress=step;return n;});
 const handlers=new Map(),document={getElementById:get,createElement:()=>new Element(),querySelector:()=>panels.find(p=>p.dataset.wizardStep==='cloud'),querySelectorAll:q=>q==='[data-wizard-step]'?panels:progress,addEventListener:(k,f)=>handlers.set(k,f)};
 const originals={MutationObserver:globalThis.MutationObserver,addEventListener:globalThis.addEventListener,scrollTo:globalThis.scrollTo};
 globalThis.MutationObserver=class{observe(){}disconnect(){}};globalThis.addEventListener=()=>{};globalThis.scrollTo=()=>{};
 const toolbar={ports:new Element(),buttons:{refresh:new Element(),status:new Element(),open:new Element()}};toolbar.ports.options=[{value:''}];
 const wizard=mountSetupWizard({document,context,toolbar});
 wizard.model.updateSession({authenticated:true,authenticatedGeneration:1,loginSupported:true});wizard.model.step='login';
 function input(username,password,confirmation){get('new-login-username').value=username;get('new-login-password').value=password;get('new-login-confirm').value=confirmation;handlers.get('input')({target:get('new-login-password')});}
 return {get,wizard,input,restore(){for(const [key,value]of Object.entries(originals))if(value===undefined)delete globalThis[key];else globalThis[key]=value;}};
}

test('actual admin/admin new-login entries show too-short guidance before Review',()=>{
 const f=fixture();try{
  f.input('admin','admin','admin');assert.equal(f.get('wizard-next').textContent,'Review my new login');assert.equal(f.get('wizard-next').disabled,true);
  assert.equal(f.get('new-login-validation').hidden,false);assert.match(f.get('new-login-validation').textContent,/too short/);
  assert.equal(f.get('new-login-password').value,'admin');assert.equal(f.get('new-login-confirm').value,'admin');
 }finally{f.restore();}
});
test('actual Finish UI waits for native close ACK and never claims worker resumption',async()=>{
 let state={authenticated:true,authenticatedGeneration:1},ack,closes=0;
 const context={getSetupState:()=>state,bluetooth:{drain:()=>new Promise(resolve=>{ack=resolve;})},closeSetup(){closes++;state={authenticated:false,authenticatedGeneration:1};f.wizard.sync();}};
 const f=fixture({context});try{
  f.wizard.model.step='finish';f.wizard.refresh();f.get('wizard-next').click();assert.equal(closes,1);assert.match(f.get('wizard-next').textContent,/Closing Bluetooth/);assert.equal(f.get('wizard-next').disabled,true);assert.equal(f.get('wizard-back').disabled,true);assert.doesNotMatch(f.get('wizard-finish-message').textContent,/confirmed.*closed/);
  ack({});await new Promise(resolve=>setImmediate(resolve));assert.match(f.get('wizard-finish-message').textContent,/confirmed its Bluetooth link closed/);assert.match(f.get('wizard-finish-message').textContent,/does not confirm.*worker resumed/);
 }finally{f.restore();}
});
test('actual Finish UI cannot turn failed native drain into closure or upload success',async()=>{
 let state={authenticated:true,authenticatedGeneration:1};const context={getSetupState:()=>state,bluetooth:{drain:async()=>{throw Error('PRIVATE OS DETAILS');}},closeSetup(){state={authenticated:false,authenticatedGeneration:1};f.wizard.sync();}};
 const f=fixture({context});try{f.wizard.model.step='finish';f.wizard.refresh();f.get('wizard-next').click();await new Promise(resolve=>setImmediate(resolve));assert.match(f.get('wizard-finish-message').textContent,/could not confirm.*Bluetooth closed/);assert.doesNotMatch(f.get('wizard-finish-message').textContent,/PRIVATE OS/);assert.equal(f.get('wizard-next').disabled,true);}finally{f.restore();}
});
test('actual records navigation requests optional records once per entry, never on unrelated render',()=>{
 let reads=0;const f=fixture({context:{getSetupState:()=>({}),readRecordedStatus(){reads++;}}});try{f.wizard.model.step='records';f.wizard.refresh();assert.equal(reads,1);for(let i=0;i<20;i++)f.wizard.refresh();assert.equal(reads,1);assert.equal(f.get('wizard-next').textContent,'Done — close Bluetooth');}finally{f.restore();}
});
test('valid private phrase enables Review without saving or changing the input',()=>{
 const f=fixture();try{
  let reviews=0;f.get('owner-login-review').addEventListener('click',()=>++reviews);f.input('admin',phrase,phrase);
  assert.equal(f.get('wizard-next').disabled,false);assert.match(f.get('new-login-validation').textContent,/not saved yet/);
  assert.equal(reviews,0);f.get('wizard-next').click();assert.equal(reviews,1);assert.equal(f.get('new-login-password').value,phrase);
 }finally{f.restore();}
});
test('confirmation mismatch and invalid username disable Review with fixed messages',()=>{
 const f=fixture();try{
  f.input('admin',phrase,'SYNTHETIC different phrase');assert.equal(f.get('wizard-next').disabled,true);assert.match(f.get('new-login-validation').textContent,/do not match/);assert.equal(f.get('new-login-confirm').attributes['aria-invalid'],'true');
  f.input('invalid@name',phrase,phrase);assert.equal(f.get('wizard-next').disabled,true);assert.match(f.get('new-login-validation').textContent,/username.*1–32/);assert.doesNotMatch(f.get('new-login-validation').textContent,/invalid@name/);
 }finally{f.restore();}
});
test('UTF-8 acceptance exactly follows the real shared policy helper',()=>{
 for(const password of ['界'.repeat(4),'💠'.repeat(3),'界'.repeat(3),'界'.repeat(43),'a'.repeat(129),phrase+'\n']){
  let valid=true;try{validateOwnerLogin('admin',password,{newPassword:true});}catch{valid=false;}
  assert.equal(newLoginFeedback('admin',password,password).valid,valid);
 }
 assert.equal(newLoginFeedback('admin','界'.repeat(4),'界'.repeat(4)).valid,true);
 assert.match(newLoginFeedback('admin','界'.repeat(43),'界'.repeat(43)).message,/too long/);
});
test('status messages never echo credentials or raw validation errors',()=>{
 for(const [username,password,confirm]of [['SYNTHETIC@SECRET','SYNTHETIC-PASSWORD','SYNTHETIC-PASSWORD'],['admin','SECRET','SECRET'],['admin',phrase,'SYNTHETIC-MISMATCH'],['admin',phrase+'\n',phrase+'\n']]){
  const result=newLoginFeedback(username,password,confirm);assert.equal(result.valid,false);assert.doesNotMatch(result.message,/SYNTHETIC|SECRET|MISMATCH|OwnerRecordError|stack/);
 }
});
test('staged Save continues to require the existing explicit confirmation gate',()=>{
 const f=fixture();try{
  f.input('admin','','');f.wizard.model.updateSession({authenticated:true,authenticatedGeneration:1,loginSupported:true,loginStaged:true});f.wizard.model.step='login';
  f.get('owner-login-save').disabled=true;f.wizard.refresh();assert.equal(f.get('wizard-next').textContent,'Save new login & reconnect');assert.equal(f.get('wizard-next').disabled,true);assert.equal(f.get('new-login-validation').hidden,true);
  f.get('owner-login-save').disabled=false;f.wizard.refresh();assert.equal(f.get('wizard-next').disabled,false);
  f.wizard.model.updateSession({authenticated:true,authenticatedGeneration:1,loginSupported:true,loginStaged:true,busy:true});f.wizard.refresh();assert.equal(f.get('wizard-next').disabled,true);
 }finally{f.restore();}
});
test('visible length hint is adjacent to the password field and precedes Review',async()=>{
 const html=await readFile(new URL('../frontend/index.html',import.meta.url),'utf8');
 const password=html.indexOf('id="new-login-password"'),hint=html.indexOf('id="new-login-password-hint"'),confirm=html.indexOf('id="new-login-confirm"'),review=html.indexOf('id="owner-login-review"');
 assert.ok(password<hint&&hint<confirm&&confirm<review);assert.match(html,/“admin” alone is too short/);assert.equal([...html.matchAll(/id="new-login-validation"/g)].length,1);
});
test('invalid Enter-like hidden submitter click is stopped before original handler clears entries',()=>{
 const f=fixture();try{
  let originalCalls=0;f.get('owner-login-review').onclick=()=>{++originalCalls;f.get('new-login-password').value='';f.get('new-login-confirm').value='';};
  f.input('admin','admin','admin');const event=f.get('owner-login-review').click();
  assert.equal(event.defaultPrevented,true);assert.equal(event.stopped,true);assert.equal(originalCalls,0);
  assert.equal(f.get('new-login-password').value,'admin');assert.equal(f.get('new-login-confirm').value,'admin');assert.match(f.get('new-login-validation').textContent,/too short/);
 }finally{f.restore();}
});
test('valid hidden submitter activation reaches the unchanged original handler exactly once',()=>{
 const f=fixture();try{
  let originalCalls=0;f.get('owner-login-review').onclick=()=>{++originalCalls;f.get('new-login-password').value='';f.get('new-login-confirm').value='';};
  f.input('admin',phrase,phrase);const event=f.get('owner-login-review').click();
  assert.equal(event.defaultPrevented,false);assert.equal(event.stopped,false);assert.equal(originalCalls,1);
  assert.equal(f.get('new-login-password').value,'');assert.equal(f.get('new-login-confirm').value,'');
 }finally{f.restore();}
});
test('kept, staged and disconnected login views clear stale aria-invalid attributes',()=>{
 for(const state of ['kept','staged','disconnected']){
  const f=fixture();try{
   f.input('admin','admin','admin');assert.equal(f.get('new-login-password').attributes['aria-invalid'],'true');
   if(state==='kept')f.wizard.model.keepLogin();
   else if(state==='staged')f.wizard.model.updateSession({authenticated:true,authenticatedGeneration:1,loginSupported:true,loginStaged:true});
   else f.wizard.model.updateSession({authenticated:false});
   f.wizard.refresh();assert.equal(f.get('new-login-validation').hidden,true);
   for(const id of ['new-login-username','new-login-password','new-login-confirm'])assert.equal(f.get(id).attributes['aria-invalid'],undefined);
  }finally{f.restore();}
 }
});
