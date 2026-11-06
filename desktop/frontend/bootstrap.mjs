import {DesktopBridge,NativeBluetooth} from './bridge.mjs';
import {createDeviceChooser} from './chooser.mjs';
import {desktopContext} from './runtime.mjs';
import {usbNextStep,usbFailureHelp} from './onboarding.mjs';
import {mountSetupWizard} from './wizard.mjs';
import {DirectSetupConnector,savedAccessAvailable} from './direct-setup.mjs';
import {FirstOwnerSetup,ownerStatus} from './first-owner.mjs';
import {mountServicePanel,serviceAllowed} from './service.mjs';

const $=id=>document.getElementById(id);
const safeScalar=value=>typeof value==='boolean'||(typeof value==='number'&&Number.isFinite(value))||
 (typeof value==='string'&&value.length<=128&&!/[\x00-\x1f\x7f]/.test(value));
function makeToolbar(){
 const container=$('desktop-toolbar');container.className='desktop-tools';container.setAttribute('aria-labelledby','desktop-title');
 const heading=document.createElement('h2');heading.id='desktop-title';heading.textContent='Find your plugged-in stick';
 const intro=document.createElement('p');intro.textContent='Keep your Cloud Stick plugged in. This app uses USB to find the stick and computer Bluetooth to change its settings.';
 const label=document.createElement('label');label.htmlFor='desktop-ports';label.textContent='USB device';
 const ports=document.createElement('select');ports.id='desktop-ports';const actions=document.createElement('div');actions.className='desktop-actions';
 const buttons={};for(const [id,text]of [['refresh','Find my USB'],['status','Check this USB'],['open','Open setup connection']]){
  const b=document.createElement('button');b.type='button';b.id='desktop-'+id;b.textContent=text;b.disabled=true;buttons[id]=b;actions.append(b);
 }
 const status=document.createElement('p');status.id='desktop-status-message';status.setAttribute('role','status');status.textContent='Starting the local secure connection…';
 const facts=document.createElement('pre');facts.id='desktop-facts';facts.hidden=true;
 const finish=document.createElement('p');finish.className='hint';finish.textContent='When finished, use the on-screen Disconnect button. Leave the USB plugged in so it can resume backups. If setup closes before you finish, open the window again and reconnect.';
 const advanced=document.createElement('details'),summary=document.createElement('summary');summary.textContent='Advanced: firmware bundle review';
 const bundle=document.createElement('button');bundle.type='button';bundle.id='desktop-bundle';bundle.textContent='Review firmware bundle';bundle.disabled=true;buttons.bundle=bundle;
 const limit=document.createElement('p');limit.className='hint';limit.textContent='Bundle review only. This account-setup step does not flash firmware or install app updates. Stock or unknown firmware needs a separate reviewed installation. A first account can be created below only when supported firmware explicitly reports no owner.';advanced.append(summary,bundle,limit);
 container.append(heading,intro,label,ports,actions,status,facts,finish,advanced);
 const openSlot=$('wizard-open-slot');if(openSlot){const instructions=document.createElement('p');instructions.textContent='Get your current username and password ready below, then open the setup connection.';openSlot.append(instructions,buttons.open);}
 const empty=()=>{ports.replaceChildren();const p=document.createElement('option');p.value='';p.textContent='Choose a USB device';ports.append(p);};empty();
 return {ports,buttons,status,facts,empty};
}
const toolbar=makeToolbar();let bridge=null,bluetooth=null,chooser=null,direct=null,firstOwner=null,service=null,busy=false,stopped=false,uiEpoch=0,checkedPort=null,retirePort=null,retiring=false,retirementAttempted=false;
const wizard=mountSetupWizard({document,context:desktopContext,toolbar,getUsbBusy:()=>busy});
function serviceReady(){return !!bridge&&!stopped&&wizard.model.step==='service'&&serviceAllowed(desktopContext.getSetupState(),{selected:!!checkedPort&&checkedPort===toolbar.ports.value&&wizard.model.usbReady,usbBusy:busy,transportBusy:!!bluetooth?.active||!!direct?.running||!!firstOwner?.running||retiring,firstOwnerPending:wizard.model.firstOwner.prepared});}
service=mountServicePanel({document,canUse:serviceReady,readDiagnostics:()=>serviceRequest('usb_diagnostics'),reviewBundle:()=>serviceRequest('review_bundle'),openRecords:()=>{wizard.openRecords();service.refreshLocks();}});
for(const id of ['wizard-service-welcome','wizard-service-usb','wizard-next','wizard-back'])$(id).addEventListener('click',()=>service.refreshLocks());
function controls(){for(const b of Object.values(toolbar.buttons))b.disabled=busy||!bridge||stopped;toolbar.ports.disabled=busy||!bridge||stopped;wizard.refresh();service?.refreshLocks();}
async function serviceRequest(op){
 if(!['usb_diagnostics','review_bundle'].includes(op)||!serviceReady())throw Object.assign(Error('Service unavailable'),{code:'usb_busy'});
 const port=checkedPort,current=uiEpoch;busy=true;desktopContext.setSetupAccessPending(true);controls();
 const alive=()=>{if(current!==uiEpoch||stopped||checkedPort!==port||toolbar.ports.value!==port||desktopContext.getSetupState().authenticated)throw Object.assign(Error('Service cancelled'),{code:'cancelled'});};
 try{await bluetooth.drain();alive();const value=await bridge.request(op,op==='review_bundle'?{}:{port});alive();return value;}
 finally{desktopContext.setSetupAccessPending(false);if(current===uiEpoch){busy=false;controls();}}
}
function showFacts(data,fields){
 const lines=[];if(data&&typeof data==='object'&&!Array.isArray(data))for(const key of fields)if(Object.hasOwn(data,key)&&safeScalar(data[key]))lines.push(key.replaceAll('_',' ')+': '+String(data[key]));
 toolbar.facts.textContent=lines.join('\n');toolbar.facts.hidden=!lines.length;
}
async function usb(op){
 if(busy||!bridge||stopped)return;
 if(bluetooth?.active){toolbar.status.textContent='Disconnect the setup session before using USB controls.';return;}
 const port=toolbar.ports.value;if(['usb_status','usb_open_setup'].includes(op)&&!port){toolbar.status.textContent='Choose a USB device first.';return;}
 busy=true;controls();const current=uiEpoch;toolbar.status.textContent='Working locally…';toolbar.facts.hidden=true;
  if(op==='usb_status'||op==='usb_ports'){checkedPort=null;wizard.usbSelectionChanged();}
 try{
  const value=await bridge.request(op,port&&op!=='usb_ports'&&op!=='review_bundle'?{port}:{});if(current!==uiEpoch||stopped)return;
  if(op==='usb_ports'){
    checkedPort=null;
   if(!Array.isArray(value)||value.length>64||value.some(row=>!row||typeof row.port!=='string'||!row.port||row.port.length>256||typeof row.label!=='string'||row.label.length>256||/[\x00-\x1f\x7f]/.test(row.label)))throw Error('Invalid port list');
   toolbar.empty();for(const row of value){const p=document.createElement('option');p.value=row.port;p.textContent=row.label||row.port;toolbar.ports.append(p);}toolbar.status.textContent=usbNextStep(op,value);
  }else if(op==='usb_open_setup')toolbar.status.textContent=usbNextStep(op,value);
  else if(op==='review_bundle'){
   showFacts(value,['version','board','bytes','sha256','signature_status','can_install']);
   toolbar.status.textContent=value?.cancelled?'Bundle review cancelled.':'Local bundle review only. Device compatibility and permission to flash have not been established.';
  }else{showFacts(value,['firmware','owner_ready','owner_state','ble','wifi','saved_revision','backups_paused','uptime_ms','reset_reason']);toolbar.status.textContent=usbNextStep(op,value);}
  wizard.usbResult(op,value);
   if(op==='usb_status'){
    checkedPort=port;
    // A false owner_ready flag is never sufficient proof of a blank owner.
    // Only the exact supported firmware OWNER_STATUS response enables creation.
    let status=null;
    try{status=ownerStatus(await bridge.request('usb_owner_status',{port}));}catch{}
    if(current!==uiEpoch||stopped||toolbar.ports.value!==port)return;
    if(status)wizard.usbResult('usb_owner_status',status);
    // Only boolean availability crosses the bridge here. Explicit Create my
    // login returns an ephemeral credential to the scoped connector.
    let available=false;
    if(wizard.model.ownerReady)try{available=savedAccessAvailable(await bridge.request('usb_setup_access_status',{port}));}catch{}
    if(current!==uiEpoch||stopped||toolbar.ports.value!==port)return;
    wizard.usbResult('usb_setup_access_status',{available});
    if(available)toolbar.status.textContent='This computer has secure setup access for this stick. Click “Create my login” to choose your username and password. Nothing is saved until you confirm.';
    else if(status?.owner_state===1)toolbar.status.textContent='Supported firmware reports no owner. Choose “Create my first account” to begin. Existing accounts will not be overwritten.';
    else if(status?.owner_state===2)toolbar.status.textContent='The owner record reports a fault. Stop and use the reviewed recovery procedure. Account creation is blocked.';
   }
 }catch(error){if(current===uiEpoch&&!stopped){toolbar.status.textContent=usbFailureHelp(error);wizard.usbFailure(toolbar.status.textContent);}}
 finally{if(current===uiEpoch){busy=false;controls();}}
}
for(const [id,op]of [['refresh','usb_ports'],['status','usb_status'],['open','usb_open_setup'],['bundle','review_bundle']])toolbar.buttons[id].onclick=()=>usb(op);
function cancelFirstOwnerIfActive(){if(firstOwner?.running||firstOwner?.state().prepared)firstOwner.cancel();}
toolbar.ports.addEventListener('change',()=>{checkedPort=null;retirePort=null;retirementAttempted=false;direct?.cancel();cancelFirstOwnerIfActive();service?.clear();wizard.usbSelectionChanged();});
function teardown(){++uiEpoch;busy=false;checkedPort=null;retirePort=null;direct?.cancel();cancelFirstOwnerIfActive();service?.clear();chooser?.cancel();bluetooth?.cancel();toolbar.facts.textContent='';toolbar.facts.hidden=true;wizard.usbSelectionChanged();controls();}
// A durable Save alone is insufficient: require the wizard’s confirmed new
// login reconnect, then an explicit Finish and completed native link drain.
addEventListener('cloudstick:setup-state',()=>{
 service?.refreshLocks();
 if(!retirementAttempted&&wizard.model.loginReconnected&&checkedPort)retirePort=checkedPort;
 if(!retirementAttempted&&retirePort&&wizard.model.finished&&!desktopContext.getSetupState().authenticated&&!busy&&!retiring&&!stopped)void retireSetupAccess();
});
async function retireSetupAccess(){
 const port=retirePort,current=uiEpoch;if(!port||port!==checkedPort||port!==toolbar.ports.value)return;
 retirementAttempted=true;retiring=true;busy=true;controls();
 try{
  await bluetooth.drain();if(current!==uiEpoch||stopped||port!==checkedPort)return;
  const value=await bridge.request('usb_forget_setup_access',{port});
  if(current!==uiEpoch||stopped)return;
  if(!value||Object.keys(value).length!==1||value.forgotten!==true)throw Error('Retirement not confirmed');
  retirePort=null;wizard.usbResult('usb_setup_access_status',{available:false});$('wizard-access-retirement').textContent='';$('wizard-access-retirement').hidden=true;
 }catch{if(current===uiEpoch&&!stopped){$('wizard-access-retirement').textContent='Setup is closed. This computer could not confirm clearing its earlier setup access. Use your new username and password next time; no automatic retry was made.';$('wizard-access-retirement').hidden=false;}}
 finally{retiring=false;if(current===uiEpoch){busy=false;controls();}}
}
addEventListener('pagehide',()=>{teardown();stopped=true;service?.stop();bluetooth?.destroy();bridge?.close();controls();});
document.addEventListener('visibilitychange',()=>{if(document.visibilityState!=='visible')teardown();});
document.addEventListener('freeze',teardown);document.addEventListener('resume',teardown);
try{
 if(!globalThis.isSecureContext||!globalThis.crypto?.subtle||typeof globalThis.QWebChannel!=='function'||!globalThis.qt?.webChannelTransport)throw Error('Secure native bridge missing');
 const native=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Native bridge timeout')),10000);new globalThis.QWebChannel(globalThis.qt.webChannelTransport,channel=>{clearTimeout(timer);resolve(channel.objects.desktop);});});
 bridge=new DesktopBridge(native);chooser=createDeviceChooser(document);bluetooth=new NativeBluetooth(bridge,{chooseDevice:rows=>chooser.choose(rows)});
 desktopContext.bluetooth=bluetooth;desktopContext.secureContext=true;
 desktopContext.closeTransport=()=>{chooser.cancel();bluetooth.cancel();};await import('./app.mjs');
  direct=new DirectSetupConnector({bridge,bluetooth,prepare:()=>desktopContext.prepareSetupAccess(),connect:auth=>desktopContext.connectSetupAccess(auth),cancel:()=>desktopContext.cancelSetupAccess(),setPending:value=>desktopContext.setSetupAccessPending(value)});
  desktopContext.startDirectSetup=async()=>{
   const port=toolbar.ports.value;if(busy||stopped||!port||checkedPort!==port||!wizard.model.savedAccessAvailable||direct.running)return false;
   busy=true;controls();const current=uiEpoch;toolbar.status.textContent='Connecting securely to this stick…';
   try{const ready=await direct.start(port);return current===uiEpoch&&!stopped&&toolbar.ports.value===port&&ready&&desktopContext.getSetupState().authenticated===true;}
   catch{if(current===uiEpoch&&!stopped)toolbar.status.textContent='Saved access could not sign in. Use your current username/password instead. No login was changed.';return false;}
   finally{if(current===uiEpoch){busy=false;controls();}}
  };
  desktopContext.cancelDirectSetup=()=>direct.cancel();
  firstOwner=new FirstOwnerSetup({bridge,bluetooth,prepare:()=>desktopContext.prepareSetupAccess(),connect:auth=>desktopContext.connectFirstOwner(auth),cancel:()=>desktopContext.cancelSetupAccess(),setPending:value=>desktopContext.setSetupAccessPending(value),onState:value=>wizard.firstOwnerState(value)});
  async function firstOperation(action){
   const port=toolbar.ports.value;if(busy||stopped||!port||checkedPort!==port||direct.running)throw Error('Owner setup unavailable');
   busy=true;controls();const current=uiEpoch;
   try{return await action(port);}finally{if(current===uiEpoch){busy=false;controls();}}
  }
  desktopContext.reviewFirstOwner=args=>firstOperation(port=>{if(wizard.model.ownerState!=='missing')throw Error('Owner is not missing');return firstOwner.review(port,args);});
  desktopContext.checkFirstOwner=()=>firstOperation(()=>firstOwner.readiness());
  desktopContext.installFirstOwner=args=>firstOperation(()=>firstOwner.install(args));
  desktopContext.confirmFirstOwner=()=>firstOperation(()=>firstOwner.confirm());
  desktopContext.cancelFirstOwner=()=>firstOwner.cancel();
 toolbar.status.textContent='Ready. Click “Find my USB” to begin. Your saved settings stay unchanged until you explicitly save.';controls();
}catch{
 toolbar.status.textContent='The secure local app connection is unavailable. Restart the installed app; do not enter a password into an untrusted page.';
 $('connection-message').textContent='Local secure setup is unavailable. Login controls remain disabled.';controls();
}
