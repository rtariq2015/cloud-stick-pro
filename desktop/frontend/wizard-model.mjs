export const setupSteps=Object.freeze(['welcome','usb','auth','login','wifi','cloud','finish']);
const emptySession=()=>({authenticated:false,busy:false,loginSupported:false,loginStaged:false,
 cloudStaged:false,portalPrepared:false,wifiCandidatePending:false,wifiConfigured:false,
 wifiSavedThisSession:false,cloudConfigured:false,cloudSavedThisSession:false,
 loginSaved:false,outcomeUnknown:false,authenticatedGeneration:0});

// Navigation holds only booleans and step names. Credentials stay in the existing
// bounded input/protocol handlers; Back/Next never implicitly commit settings.
export class SetupWizardModel{
 constructor(){this.step='welcome';this.usbReady=false;this.ownerReady=false;this.savedAccessAvailable=false;this.directConnecting=false;this.directAuthenticated=false;
  this.ownerState='unknown';this.firstOwner={busy:false,prepared:false,canEnroll:false,installed:false,unknown:false,confirmed:false,expired:false};
  this.state=emptySession();this.keptLogin=false;this.loginReconnect=false;
  this.loginReconnected=false;this.loginSaveGeneration=0;this.skippedCloud=false;
  this.finishing=false;this.finished=false;this.notice='';this.returnToRecords=false;}
 canOpenService(selected){return selected===true&&this.usbReady&&['welcome','usb'].includes(this.step)&&!this.state.authenticated&&!this.locked()&&!this.state.wifiCandidatePending&&!this.directConnecting&&!this.firstOwner.prepared&&!this.firstOwner.busy&&!this.finishing;}
 openService(selected){if(!this.canOpenService(selected))return false;this.serviceReturn=this.step;this.step='service';this.notice='';return true;}
 closeService(){if(this.step!=='service'||this.locked())return false;this.step=this.serviceReturn==='welcome'?'welcome':'usb';return true;}
 resetUsb(){this.usbReady=false;this.ownerReady=false;this.savedAccessAvailable=false;this.directConnecting=false;this.directAuthenticated=false;this.keptLogin=false;
  this.ownerState='unknown';
  this.loginReconnect=false;this.loginReconnected=false;this.finished=false;
  if(this.step!=='welcome')this.step='usb';}
 checkedUsb({ownerReady}){this.usbReady=true;this.ownerReady=ownerReady===true;this.savedAccessAvailable=false;}
 checkedOwner(state){this.ownerState=state===0?'ready':state===1?'missing':state===2?'fault':'unknown';if(state===0)this.ownerReady=true;else if(state===1||state===2)this.ownerReady=false;}
 startFirstOwner(){if(!this.usbReady||this.ownerState!=='missing'||this.firstOwner.unknown||this.firstOwner.installed||this.locked())return false;this.step='first-owner';this.notice='';return true;}
 updateFirstOwner(value){
  this.firstOwner=Object.fromEntries(Object.keys(this.firstOwner).map(k=>[k,value?.[k]===true]));
  if(this.firstOwner.unknown){this.step='auth';this.notice='first-owner-unknown';}
  else if(this.firstOwner.confirmed&&this.state.authenticated){this.ownerReady=true;this.ownerState='ready';this.step='wifi';this.notice='first-owner-confirmed';}
  else if((this.firstOwner.installed||this.firstOwner.expired)&&!this.firstOwner.prepared&&!this.firstOwner.busy){this.step='auth';this.notice='first-owner-signin';}
 }
 savedAccess(available){this.savedAccessAvailable=this.usbReady&&this.ownerReady&&available===true;}
 beginDirect(){if(!this.savedAccessAvailable||this.state.authenticated||this.locked()||this.directConnecting||this.state.outcomeUnknown||this.loginReconnect)return false;this.directConnecting=true;this.notice='';return true;}
 endDirect(success){this.directConnecting=false;this.savedAccessAvailable=false;
  if(success&&this.state.authenticated&&this.state.loginSupported){this.directAuthenticated=true;this.step='login';this.notice='';}
  else{this.step='auth';this.notice='saved-access-failed';}}
 updateSession(raw){
  const previous=this.state,next=emptySession();
  for(const key of Object.keys(next))if(key!=='authenticatedGeneration')next[key]=raw?.[key]===true;
  next.authenticatedGeneration=Number.isSafeInteger(raw?.authenticatedGeneration)&&raw.authenticatedGeneration>=0?raw.authenticatedGeneration:0;
  this.state=next;
  if(next.outcomeUnknown){this.finishing=false;this.finished=false;this.step='auth';this.notice='unknown-save';return;}
  if(next.loginSaved&&!previous.loginSaved){
   this.loginReconnect=true;this.loginReconnected=false;this.loginSaveGeneration=previous.authenticatedGeneration;
   this.keptLogin=false;this.step='auth';this.notice='new-login';return;
  }
  if(next.authenticated&&!previous.authenticated){
   if(this.loginReconnect&&next.authenticatedGeneration>this.loginSaveGeneration){
    this.loginReconnect=false;this.loginReconnected=true;this.notice='login-confirmed';
   }else if(!this.loginReconnect)this.notice='';
  }
  if(previous.authenticated&&!next.authenticated){
    this.directAuthenticated=false;
   if(this.finishing){this.finished=true;this.finishing=false;this.step='finish';this.notice='finished';}
   else if(!this.loginReconnect){this.step='auth';this.notice='disconnected';this.finished=false;}
  }
 }
 locked(){return this.state.busy||this.state.loginStaged||this.state.cloudStaged||this.state.portalPrepared;}
 canNext(){
  if(this.locked()||this.state.outcomeUnknown)return false;
  if(this.step==='welcome')return true;
  if(this.step==='usb')return this.usbReady&&this.ownerReady;
  if(this.step==='auth')return this.state.authenticated;
  if(this.step==='login')return this.state.authenticated&&(this.keptLogin||this.loginReconnected);
  if(this.step==='wifi')return this.state.authenticated&&!this.state.wifiCandidatePending&&
   (this.state.wifiConfigured||this.state.wifiSavedThisSession);
  if(this.step==='cloud')return this.state.authenticated&&(this.skippedCloud||this.state.cloudConfigured||this.state.cloudSavedThisSession);
  return false;
 }
 next(){
  if(!this.canNext())return false;
  if(this.step==='auth')this.step=this.returnToRecords?'records':this.loginReconnected?'wifi':'login';
  else this.step=setupSteps[setupSteps.indexOf(this.step)+1];
  this.notice='';return true;
 }
 back(){
  if(this.locked()||this.finishing)return false;
  if(!this.state.authenticated&&['login','wifi','cloud','finish','records'].includes(this.step)){
   this.step='auth';this.finished=false;this.notice='disconnected';return true;
  }
  if(this.step==='records'){this.step='finish';return true;}
  if(this.step==='welcome')return false;
  if(this.step==='auth'&&this.loginReconnect){this.step='usb';return true;}
  const index=setupSteps.indexOf(this.step);if(index<1)return false;
  this.step=setupSteps[index-1];this.finished=false;return true;
 }
 keepLogin(){if(!this.state.authenticated||this.locked())return false;this.keptLogin=true;return true;}
 skipCloud(){if(this.step!=='cloud'||!this.state.authenticated||this.locked())return false;this.skippedCloud=true;return true;}
 startFinish(){
  if(this.step!=='finish'||!this.state.authenticated||this.locked()||this.state.outcomeUnknown)return false;
  this.finishing=true;return true;
 }
 records(){if(this.locked())return false;this.returnToRecords=true;
  this.step=this.state.authenticated?'records':'auth';this.notice=this.state.authenticated?'':'records-login';return true;}
}
