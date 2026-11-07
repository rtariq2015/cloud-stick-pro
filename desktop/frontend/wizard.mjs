import {SetupWizardModel,setupSteps} from './wizard-model.mjs';
import {validateOwnerLogin} from './shared/security2.mjs';

// The existing official-protocol helper owns the policy. Only fixed explanatory
// text is returned here; entered values and exception messages are never echoed.
export function newLoginFeedback(username,password,confirmation){
 try{validateOwnerLogin(username,password,{newPassword:true});}
 catch(error){
  if(error?.code==='username')return {valid:false,field:'username',message:'Choose a username with 1–32 letters, numbers, dots, dashes or underscores.'};
  const bytes=typeof password==='string'?new TextEncoder().encode(password).length:0;
  if(bytes<12)return {valid:false,field:'password',message:'This password is too short. Choose a memorable phrase of at least 12 English characters; spaces count. “admin” alone is too short.'};
  if(bytes>128)return {valid:false,field:'password',message:'This password is too long. Use a shorter phrase, up to 128 UTF-8 bytes.'};
  return {valid:false,field:'password',message:'Use a normal text passphrase without control characters.'};
 }
 if(typeof confirmation!=='string'||!confirmation)return {valid:false,field:'confirmation',message:'Repeat your new password in the second box.'};
 if(password!==confirmation)return {valid:false,field:'confirmation',message:'The passwords do not match yet. Type the same passphrase in both boxes.'};
 return {valid:true,field:null,message:'The entries match and meet the length rules. Review your new login when ready; it is not saved yet.'};
}

const copy={
 welcome:['Let’s set up your Cloud Stick.','We’ll guide you through connecting, choosing your login, Wi-Fi and cloud backup. Your existing files stay on the USB.'],
 usb:['Connect your USB','Leave the stick plugged in. Find it below, select it, then check it. On a computer with saved setup access, you can go straight to choosing your login.'],
 auth:['Sign in with your login','Use the username and password currently saved on this stick. Advanced recovery-key sign-in is available if you need it.'],
 login:['Choose your username and password','Make this stick yours with a login you can remember. Review and explicitly save it, then sign in again to confirm it worked.'],
 wifi:['Choose your Wi-Fi','Find a network, check the connection, then save it. You can also keep the working network already saved on your stick.'],
 cloud:['Set up cloud backup','Review the Koofr account and destination. Keep your saved setup, or enter and explicitly save new details.'],
 finish:['Your setup summary','Settings saved on the stick are separate from a verified cloud backup. Finish by closing the setup connection and leaving the USB plugged in.'],
 records:['Your backup records','These are the stick’s saved results from this authenticated connection. They do not run a new upload or cloud check.'],
 'first-owner':['Create the first account','Choose a username and password, review them locally, then explicitly create the account using the stick’s physical BOOT confirmation.'],
 service:['Troubleshoot and updates','Read a safe local snapshot or review a firmware bundle. These checks do not change settings or test the cloud account.']
};
const notices={
 'new-login':'Your new login was saved. Open the connection window again and sign in using your new username and password to continue.',
 'login-confirmed':'Your new login works. Continue to Wi-Fi.',
 'unknown-save':'A connection ended before a save was confirmed. Sign in again and inspect the saved settings before repeating a save.',
 disconnected:'The setup connection ended. Open the short window and sign in again to continue. Leave the USB plugged in.',
 'records-login':'Sign in again to read the stick’s recorded backup status.',
 'saved-access-failed':'This computer’s saved access could not finish signing in. No login was changed. Use the username and password currently on this stick, or the advanced recovery-key option.',
 'first-owner-unknown':'Account creation was not conclusively confirmed. Do not repeat Create account. Open the setup connection and try signing in with the username and password you chose; use recovery if that fails.',
 'first-owner-signin':'Use the username and password you chose to check the stick. Do not repeat account creation. Prepared details were cleared from this app.',
 'first-owner-confirmed':'Your first account was created and your chosen login was verified securely. Continue with Wi-Fi.',
 finished:'The setup session is cleared in this app. Bluetooth closure is confirmed separately below.'
};

export function mountSetupWizard({document,context,toolbar,getUsbBusy=()=>false}){
 const $=id=>document.getElementById(id),model=new SetupWizardModel();
 const next=$('wizard-next'),back=$('wizard-back'),title=$('wizard-title');
 let shown=null,forwardAfter=null,cloudEdited=false,refreshing=false,directAttempt=0;
 let closePending=false,closeOutcome=null,closeAttempt=0;
 async function finishConnection(){
  if(!model.startFinish())return;
  const attempt=++closeAttempt;closePending=true;closeOutcome=null;refresh();
  try{
   // Clearing local authentication is synchronous; native radio ownership is
   // released only when the retained disconnect promise acknowledges it.
   context.closeSetup?.();
   if(typeof context.bluetooth?.drain!=='function')throw Error('No native close acknowledgement');
   await context.bluetooth.drain();if(attempt===closeAttempt)closeOutcome='acknowledged';
  }catch{if(attempt===closeAttempt)closeOutcome='unknown';}
  finally{if(attempt===closeAttempt){closePending=false;refresh();}}
 }
  const clearFirstFields=()=>{for(const id of ['first-owner-username','first-owner-password','first-owner-confirm'])$(id).value='';$('first-owner-accept').checked=false;};
  const firstFeedback=()=>newLoginFeedback($('first-owner-username').value,$('first-owner-password').value,$('first-owner-confirm').value);
  async function firstAction(op){
   const attempt=++directAttempt;let args=null;
   if(op==='review'){
    if(!firstFeedback().valid)return;
    args={username:$('first-owner-username').value,password:$('first-owner-password').value,confirmation:$('first-owner-confirm').value};clearFirstFields();
   }
   try{
    const pending=op==='review'?context.reviewFirstOwner?.(args):op==='check'?context.checkFirstOwner?.():op==='install'?context.installFirstOwner?.({accept:$('first-owner-accept').checked===true}):context.confirmFirstOwner?.();
    if(args){args.username='';args.password='';args.confirmation='';args=null;}
    const result=await pending;if(attempt!==directAttempt)return;
    $('first-owner-message').textContent=op==='review'?'Login prepared locally. Follow the microSD and BOOT instructions, then check readiness.':op==='check'?result===true?'Ready. Keep BOOT held, confirm the checkbox, then explicitly create the account.':'Not ready yet. Keep BOOT held for at least 10 seconds, make sure the microSD is out and no other setup is active.':op==='install'?'Account saved on this USB. Release BOOT now, then continue to verify your chosen login.':result===true?'Your chosen login was verified.':'Release BOOT fully, then continue again to verify your login.';
   }catch(error){if(attempt===directAttempt)$('first-owner-message').textContent=error?.code==='owner_login_encoding'?'This username/password combination could not be prepared securely for this firmware. Choose a different passphrase.':error?.code==='owner_install_unknown'?'Account creation is unconfirmed. Do not repeat Create. Try signing in with the login you chose.':error?.code==='owner_confirmation_needed'?'Account creation was confirmed, but sign-in was not. Use the login you chose to sign in; do not create another account.':'The account setup could not finish. Check the USB again. No automatic retry was made.';}
   finally{if(args){args.username='';args.password='';args.confirmation='';}refresh();}
  }
 const loginFeedback=()=>newLoginFeedback($('new-login-username').value,$('new-login-password').value,$('new-login-confirm').value);
 function paintLoginFeedback(){
  const message=$('new-login-validation'),relevant=model.step==='login'&&!model.state.loginStaged&&!model.keptLogin&&!model.loginReconnected;
  message.hidden=!relevant;if(!relevant){message.textContent='';for(const id of ['new-login-username','new-login-password','new-login-confirm'])$(id).removeAttribute('aria-invalid');return;}
  const result=loginFeedback();message.textContent=result.message;
  for(const [id,field]of [['new-login-username','username'],['new-login-password','password'],['new-login-confirm','confirmation']])$(id).setAttribute('aria-invalid',!result.valid&&result.field===field?'true':'false');
 }
 const openMessage=document.createElement('p');openMessage.id='wizard-setup-message';openMessage.className='hint';openMessage.setAttribute('role','status');$('wizard-open-slot').append(openMessage);
 const later=document.createElement('button');later.id='wizard-skip-cloud';later.type='button';later.className='text-button';later.textContent='Set up cloud backup later';
 const laterHint=document.createElement('p');laterHint.className='hint';laterHint.textContent='Skipping does not set up a cloud destination. Files cannot upload until usable cloud settings are saved.';
 document.querySelector('[data-wizard-step="cloud"]').append(later,laterHint);
 const portalReturn=document.createElement('button');portalReturn.type='button';portalReturn.className='text-button';portalReturn.textContent='Return to sign-in to check the stick';$('portal-handoff-card').append(portalReturn);
 portalReturn.addEventListener('click',()=>{model.step='auth';model.notice='disconnected';refresh();});
 const personal=document.createElement('button');personal.id='wizard-personal-signin';personal.type='button';personal.className='wizard-secondary';personal.textContent='Sign in with my username/password';$('desktop-toolbar').append(personal);
 const cancelDirect=document.createElement('button');cancelDirect.id='wizard-cancel-direct';cancelDirect.type='button';cancelDirect.className='text-button';cancelDirect.textContent='Cancel secure connection';$('desktop-toolbar').append(cancelDirect);
 async function directSetup(){
  if(!model.beginDirect())return;const attempt=++directAttempt;refresh();
  try{const ready=await context.startDirectSetup?.();if(attempt===directAttempt)model.endDirect(ready===true);}catch{if(attempt===directAttempt)model.endDirect(false);}refresh();
 }
 function action(){
   if(closePending)return {label:'Closing Bluetooth…',disabled:true};
   if(closeOutcome==='unknown')return {label:'Close not confirmed',disabled:true};
  if(model.step==='service')return {label:'Back to setup',disabled:getUsbBusy()||model.state.busy,run:()=>model.closeService()};
  if(model.step==='first-owner'){
   const state=model.firstOwner;
   if(getUsbBusy()||state.busy)return {label:'Working securely…',disabled:true};
   if(state.unknown||state.expired)return {label:'Sign in to check the account',disabled:true};
   if(state.installed)return {label:'I released BOOT — verify my login',run:()=>firstAction('confirm')};
   if(state.prepared)return state.canEnroll?{label:'Create account on this USB',disabled:!$('first-owner-accept').checked,run:()=>firstAction('install')}:{label:'Check BOOT readiness',run:()=>firstAction('check')};
   return {label:'Review my first account',disabled:!firstFeedback().valid||typeof context.reviewFirstOwner!=='function',run:()=>firstAction('review')};
  }
  if(model.directConnecting)return {label:'Connecting securely…',disabled:true};
  if(getUsbBusy()||model.state.busy)return {label:'Please wait…',disabled:true};
  if(model.state.outcomeUnknown&&model.step!=='auth')return {label:'Reconnect to check saved settings',disabled:true};
  if(model.step==='welcome')return {label:'Start setup',run:()=>model.next()};
  if(model.step==='usb'){
   if(!toolbar.ports.value)return toolbar.ports.options.length>1?
    {label:'Choose your USB above',disabled:true}:
    {label:'Find my USB',disabled:toolbar.buttons.refresh.disabled,run:()=>toolbar.buttons.refresh.click()};
   if(!model.usbReady)return {label:'Check this USB',disabled:toolbar.buttons.status.disabled,run:()=>toolbar.buttons.status.click()};
    if(model.ownerState==='missing')return {label:'Create my first account',disabled:model.firstOwner.unknown||model.firstOwner.installed,run:()=>model.startFirstOwner()};
    if(model.ownerState==='fault')return {label:'Owner record needs recovery',disabled:true};
    if(model.savedAccessAvailable)return {label:'Create my login',disabled:!model.canNext()||typeof context.startDirectSetup!=='function',run:directSetup};
    return {label:'Next: sign in',disabled:!model.canNext(),run:()=>model.next()};
  }
  if(model.step==='auth')return model.state.authenticated?
   {label:model.returnToRecords?'View backup records':'Next: '+(model.loginReconnected?'Wi-Fi':'your login'),disabled:!model.canNext(),run:()=>model.next()}:
   {label:'Connect securely',disabled:$('connect').disabled||!($('owner-key').value||($('login-username').value&&$('login-password').value)),run:()=>$('connect').click()};
  if(model.step==='login'){
   if(model.keptLogin||model.loginReconnected)return {label:'Next: Wi-Fi',disabled:!model.canNext(),run:()=>model.next()};
   if(model.state.loginStaged)return {label:'Save new login & reconnect',disabled:$('owner-login-save').disabled,run:()=>$('owner-login-save').click()};
   return {label:'Review my new login',disabled:$('owner-login-review').disabled||!loginFeedback().valid,run:()=>$('owner-login-review').click()};
  }
  if(model.step==='wifi'){
   if(model.state.wifiCandidatePending){
    if(!$('save-panel').hidden)return {label:'Save Wi-Fi & continue',disabled:$('save').disabled,run:()=>{forwardAfter='wifi';$('save').click();}};
    return {label:'Check this Wi-Fi',disabled:$('check').disabled,run:()=>$('check').click()};
   }
   return {label:'Next: cloud backup',disabled:!model.canNext(),run:()=>model.next()};
  }
  if(model.step==='cloud'){
   if(model.state.cloudStaged)return {label:'Save cloud settings & continue',disabled:$('cloud-save').disabled,run:()=>{forwardAfter='cloud';$('cloud-save').click();}};
   if((cloudEdited&&$('cloud-finish').hidden)||!model.state.cloudConfigured)return {label:'Review cloud settings',disabled:$('cloud-review').disabled,run:()=>$('cloud-review').click()};
   return {label:'Next: finish',disabled:!model.canNext(),run:()=>model.next()};
  }
   if(model.step==='finish')return {label:model.finished?closeOutcome==='acknowledged'?'App link closed':'Setup session cleared':'Finish setup',disabled:model.finished||!model.state.authenticated||model.locked(),run:finishConnection};
   if(model.step==='records')return {label:'Done — close Bluetooth',disabled:model.locked(),run:()=>{model.step='finish';return finishConnection();}};
  return {label:'Back to summary',run:()=>model.back()};
 }
 function refresh(){
  if(refreshing)return;refreshing=true;
  try{
   const step=model.step,content=copy[step];
    const first=model.firstOwner,returnCard=first.confirmed;
    $('wizard-card-reminder').hidden=!returnCard;
    $('wizard-finish-title').textContent=returnCard?'Check your microSD, then leave it plugged in.':'Ready to leave it plugged in.';
    $('wizard-finish-hint').textContent=returnCard?'After Finish confirms the app link closed, follow the microSD reminder if the card is still out. Insert it only while the stick is unplugged.':'Disconnect means closing the app’s Bluetooth session. Leave the USB physically plugged in.';
    for(const id of ['first-owner-username','first-owner-password','first-owner-confirm'])$(id).disabled=first.busy||first.prepared||first.installed||first.unknown;
    $('first-owner-review').hidden=!first.prepared||first.installed;$('first-owner-accept').disabled=first.busy||!first.canEnroll||first.installed;
    $('first-owner-validation').hidden=step!=='first-owner'||first.prepared||first.installed;
    const firstValidation=$('first-owner-validation').hidden?null:firstFeedback();$('first-owner-validation').textContent=firstValidation?.message??'';
    for(const [id,field]of [['first-owner-username','username'],['first-owner-password','password'],['first-owner-confirm','confirmation']]){
     if(firstValidation)$(id).setAttribute('aria-invalid',!firstValidation.valid&&firstValidation.field===field?'true':'false');else $(id).removeAttribute('aria-invalid');
    }
    paintLoginFeedback();
   for(const panel of document.querySelectorAll('[data-wizard-step]'))panel.hidden=panel.dataset.wizardStep!==step;
    $('wizard-step-label').textContent=step==='service'?'DEVICE SERVICE':step==='first-owner'?'FIRST ACCOUNT':step==='welcome'?'GUIDED SETUP':step==='records'?'RECORDED STATUS':'STEP '+setupSteps.indexOf(step)+' OF 6';
   title.textContent=content[0];$('wizard-description').textContent=content[1];
   $('wizard-message').textContent=notices[model.notice]||
     (step==='usb'&&model.ownerState==='fault'?'The owner record reports a fault. Stop and use the reviewed recovery procedure; creating an account is blocked.':step==='usb'&&model.ownerState==='missing'?'Supported firmware reports no owner. Create your first username/password account below; no existing owner will be overwritten.':step==='usb'&&model.usbReady&&!model.ownerReady?'No exact supported Missing-owner status was confirmed. Account creation is unavailable. Use Install or update your stick above for a supported new original board. Unknown firmware needs review.':'');
   $('wizard-message').hidden=!$('wizard-message').textContent;
    $('wizard-position').textContent=step==='service'?'Local checks':step==='first-owner'?'New owner':step==='welcome'?'One step at a time':step==='records'?'Saved device results':'Step '+setupSteps.indexOf(step)+' of 6';
   for(const item of document.querySelectorAll('[data-wizard-progress]')){
    const current=item.dataset.wizardProgress===step;item.toggleAttribute('aria-current',current);if(current)item.setAttribute('aria-current','step');
    item.classList.toggle('is-complete',setupSteps.indexOf(item.dataset.wizardProgress)<setupSteps.indexOf(step));
   }
   const a=action();next.textContent=a.label;next.disabled=a.disabled===true;
    back.hidden=step==='welcome'||step==='service';back.disabled=closePending||closeOutcome==='unknown'||(step==='first-owner'?first.busy||getUsbBusy():model.locked()||getUsbBusy()||model.finishing);
    for(const id of ['wizard-service-welcome','wizard-service-usb'])$(id).disabled=getUsbBusy()||!model.canOpenService(!!toolbar.ports.value);
   $('wizard-keep-login').disabled=!model.state.authenticated||model.locked();
    personal.hidden=step!=='usb'||!model.usbReady||!model.ownerReady;personal.disabled=getUsbBusy()||model.state.busy||model.directConnecting;
    cancelDirect.hidden=!model.directConnecting;
   later.disabled=!model.state.authenticated||model.locked();
   $('wizard-disconnect').disabled=!model.state.authenticated||model.locked();
   $('wizard-records').disabled=model.locked()||closePending||closeOutcome==='unknown';
   $('wizard-auth-hint').textContent=model.loginReconnect?'Use the new username and password you just saved. Your previous key was replaced.':
     'Enter your current username and password, then click Open setup connection. Finish closes Bluetooth so backups can resume.';
   toolbar.ports.disabled=getUsbBusy()||model.state.busy||model.state.authenticated||toolbar.buttons.refresh.disabled;
   toolbar.buttons.open.disabled=getUsbBusy()||model.state.busy||model.state.authenticated||toolbar.buttons.refresh.disabled;
   $('wizard-finish-message').textContent=closePending?'Closing the app’s Bluetooth link. Keep the USB plugged in.':
    closeOutcome==='acknowledged'&&returnCard?'Bluetooth is closed. If the microSD is still out, safely eject any drive shown, unplug the stick, insert the card, and reconnect normally. Your login and settings stay saved. Leave power connected, then try a small file and check its backup result.':
    closeOutcome==='acknowledged'?'The app confirmed its Bluetooth link closed. Leave the USB plugged in. This does not confirm that the backup worker resumed or that a new upload passed.':
    closeOutcome==='unknown'?'The app could not confirm that Bluetooth closed. Keep the USB powered and close/reopen the app before another setup attempt. Backup resumption is not confirmed.':model.finished?notices.finished:
    (model.skippedCloud?'Cloud backup was left for later. This is not a completed cloud-backup setup. Close the setup connection when you are ready.':
     'Your Wi-Fi and cloud settings are present on the stick. This does not prove a new upload. Finish setup, then check the local dashboard or reconnect later for backup records.');
   if(shown!==step){shown=step;title.focus({preventScroll:true});globalThis.scrollTo?.({top:0,behavior:'instant'});if(step==='records')void context.readRecordedStatus?.();}
  }finally{refreshing=false;}
 }
 function sync(){
  const state=context.getSetupState?.();if(state)model.updateSession(state);
   if(state?.authenticated===true&&!closePending)closeOutcome=null;
  if(forwardAfter&&!model.state.busy){
   const expected=forwardAfter==='wifi'?model.state.wifiSavedThisSession:model.state.cloudSavedThisSession;
   if(expected&&model.state.authenticated&&!model.state.outcomeUnknown&&model.canNext()){forwardAfter=null;cloudEdited=false;model.next();}
   else if(!model.state.authenticated||model.state.outcomeUnknown)forwardAfter=null;
  }
  refresh();
 }
 // Implicit Enter submission may activate the original hidden submit button
 // before the form's submit handler runs. Guard that activation in capture
 // phase, before the existing handler can clear input fields. Valid activation
 // still reaches the unchanged original handler and explicit save barrier.
 $('owner-login-review').addEventListener('click',event=>{
  if(!loginFeedback().valid){event.preventDefault();event.stopImmediatePropagation();refresh();}
 },{capture:true});
 next.addEventListener('click',()=>{const a=action();if(!a.disabled){a.run?.();refresh();}});
 back.addEventListener('click',()=>{if(!getUsbBusy()){
   if(model.step==='first-owner'){++directAttempt;clearFirstFields();context.cancelFirstOwner?.();model.step=model.firstOwner.installed||model.firstOwner.unknown?'auth':'usb';}
   else if(model.directAuthenticated&&model.step==='login'&&!model.locked())context.closeSetup?.();
  else model.back();
 }refresh();});
 personal.addEventListener('click',()=>{if(!getUsbBusy()&&!model.directConnecting&&model.usbReady&&model.ownerReady){model.step='auth';model.notice='';refresh();}});
 cancelDirect.addEventListener('click',()=>{++directAttempt;context.cancelDirectSetup?.();model.endDirect(false);refresh();});
  $('first-owner-form').onsubmit=event=>event.preventDefault();
  $('first-owner-cancel').addEventListener('click',()=>{++directAttempt;clearFirstFields();context.cancelFirstOwner?.();model.step=model.firstOwner.installed||model.firstOwner.unknown?'auth':'usb';refresh();});
 $('wizard-keep-login').addEventListener('click',()=>{model.keepLogin();model.next();refresh();});
 later.addEventListener('click',()=>{model.skipCloud();model.next();refresh();});
 $('wizard-disconnect').addEventListener('click',()=>{void finishConnection();refresh();});
 $('wizard-records').addEventListener('click',()=>{model.records();refresh();});
  for(const id of ['wizard-service-welcome','wizard-service-usb'])$(id).addEventListener('click',()=>{if(!getUsbBusy())model.openService(!!toolbar.ports.value);refresh();});
 document.addEventListener('input',()=>{refresh();});
 document.addEventListener('change',event=>{if(['cloud-email','cloud-password','cloud-folder','cloud-keep-login','cloud-keep-folder'].includes(event.target?.id))cloudEdited=true;refresh();});
 for(const id of ['cloud-email','cloud-password','cloud-folder'])$(id).addEventListener('input',()=>{cloudEdited=true;refresh();});
 globalThis.addEventListener('cloudstick:setup-state',sync);
 // Existing handlers own disabled/hidden state. Observe only those attributes;
 // text updates above never cause an observation/render loop.
 const observer=new MutationObserver(()=>refresh());
 for(const id of ['connect','owner-login-save','save','check','cloud-review','cloud-save','save-panel'])observer.observe($(id),{attributes:true,attributeFilter:['disabled','hidden']});
 const portalObserver=new MutationObserver(()=>{if(!$('portal-handoff-card').hidden&&!model.state.authenticated){model.step='wifi';model.notice='';refresh();}});
 portalObserver.observe($('portal-handoff-card'),{attributes:true,attributeFilter:['hidden']});
 refresh();
 return {refresh,sync,model,
   openRecords(){if(!getUsbBusy())model.records();refresh();},
   usbSelectionChanged(){++directAttempt;++closeAttempt;closePending=false;closeOutcome=null;clearFirstFields();model.resetUsb();openMessage.textContent='';refresh();},
   firstOwnerState(value){model.updateFirstOwner(value);if(value?.unknown||value?.expired||value?.confirmed)clearFirstFields();refresh();},
  usbFailure(message){openMessage.textContent=message;refresh();},
  usbResult(op,value){
   if(op==='usb_ports')model.resetUsb();
   if(op==='usb_status')model.checkedUsb({ownerReady:value?.owner_ready});
    if(op==='usb_owner_status')model.checkedOwner(value?.owner_state);
    if(op==='usb_setup_access_status')model.savedAccess(value?.available);
   if(op==='usb_open_setup')openMessage.textContent=value?.accepted===true?'Setup window is open. Click Connect securely now.':'Setup could not open. Finish other work, then try again or use BOOT for three seconds.';
   refresh();
  }};
}
