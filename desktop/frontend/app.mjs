import {ManagementConnection,WifiSetupClient,WifiInputError,PublicWifiClient,DiagnosticLogClient,FileHistoryClient,CloudSetupClient,CloudInputError,validateCloudFields,OwnerLoginClient,OwnerLoginInputError,OwnerRecordError,validateOwnerLogin} from './shared/connection.mjs';
import {desktopContext,publishSetupState} from './runtime.mjs';
const $=id=>document.getElementById(id);
let connection=null,flow=null,epoch=0,busy=false,profiles=[],selected=null;
let ownerCardDevice=null;
let portal=null,portalNetwork=null,portalPrepared=null,portalInstructions=null;
let cloud=null,cloudSnapshot=null,cloudStaged=false,cloudSaving=false,cloudWifiReady=false;
let ownerLogin=null,loginStaged=false,loginSaving=false;
let fullyAuthenticated=false,authenticatedGeneration=0,wifiConfigured=false,wifiCandidatePending=false;
let wifiSavedThisSession=false,cloudSavedThisSession=false,loginSaved=false;
let wifiSaving=false,wifiSaveUnknown=false,cloudSaveUnknown=false,loginOutcomeUnknown=false;
let setupAccessPreparing=false,setupAccessPending=false;
let recordCapabilities=null,recordsAttempted=false;
function publishWizard(){
 publishSetupState({authenticated:fullyAuthenticated&&!!flow,busy:!!busy||setupAccessPending,loginSupported:!!ownerLogin,
  loginStaged:!!loginStaged,cloudStaged:!!cloudStaged,portalPrepared:!!portalPrepared,
  wifiConfigured:!!wifiConfigured,wifiCandidatePending:!!wifiCandidatePending,wifiSavedThisSession:!!wifiSavedThisSession,
  cloudConfigured:cloudSnapshot?.configured===true&&cloudSnapshot?.store_fault===false,
  cloudSavedThisSession:!!cloudSavedThisSession,loginSaved:!!loginSaved,wifiSaveUnknown:!!wifiSaveUnknown,
  cloudSaveUnknown:!!cloudSaveUnknown,outcomeUnknown:loginOutcomeUnknown||wifiSaveUnknown||cloudSaveUnknown,authenticatedGeneration});
}
function clearWizardLive(){
 recordCapabilities=null;recordsAttempted=false;
 $('records-message').textContent='Sign in to read a saved snapshot.';$('records-read-at').textContent='';
 if(wifiSaving)wifiSaveUnknown=true;
 wifiSaving=false;fullyAuthenticated=false;wifiConfigured=false;wifiCandidatePending=false;
 wifiSavedThisSession=false;cloudSavedThisSession=false;
}
function savedWifiState(saved,rows){return saved?.durable===true&&saved.store_fault===false&&rows.some(p=>p.enabled===true&&p.id===saved.selected_profile);}
function markWifiCandidate(){wifiCandidatePending=true;wifiSavedThisSession=false;}
function clearLogin(){
 if(loginSaving){loginOutcomeUnknown=true;loginSaved=false;$('owner-login-outcome').hidden=false;$('owner-login-outcome').textContent='The connection ended during login Save. The outcome is unknown. Reconnect using your new login; if it fails, try your previous login or recovery route. Do not repeat Save automatically.';}
 ownerLogin=null;loginStaged=false;loginSaving=false;
 $('owner-login-card').hidden=true;$('owner-login-confirm-panel').hidden=true;$('owner-login-accept').checked=false;$('owner-login-message').textContent='';
 for(const id of ['login-username','login-password','new-login-username','new-login-password','new-login-confirm'])$(id).value='';
 $('login-password').type='password';$('show-login-password').checked=false;
}
function clearCloud(){
 if(cloudSaving){cloudSaveUnknown=true;$('cloud-outcome').hidden=false;$('cloud-outcome').textContent='The connection ended during cloud Save. The outcome is unknown. Reconnect to read the cloud settings revision before trying again.';}
 cloud=null;cloudSnapshot=null;cloudStaged=false;cloudSaving=false;cloudWifiReady=false;
 $('cloud-card').hidden=true;$('cloud-confirm').hidden=true;$('cloud-finish').hidden=true;
 $('cloud-keep-login-row').hidden=true;$('cloud-keep-folder-row').hidden=true;
 for(const id of ['cloud-email','cloud-password','cloud-folder'])$(id).value='';
 for(const id of ['cloud-keep-login','cloud-keep-folder','cloud-accept'])$(id).checked=false;
 $('cloud-status').textContent='';$('cloud-message').textContent='';
}
function paintCloud(snapshot){
 // A new saved destination/revision invalidates this optional view. Old
 // verified counts must not be presented under the newly selected scope.
 if(cloudSnapshot&&cloudSnapshot.revision!==snapshot.revision){recordsAttempted=false;clearBackup();clearActivity();clearFileHistory();}
 cloudSnapshot=snapshot;
 $('cloud-status').textContent=snapshot.store_fault?'Saved cloud settings need review. Do not retry Save until the stick is checked.':
  snapshot.provider==='other'?'A different provider is saved. This page supports Koofr only. Review all fields before replacing it.':
  snapshot.configured?'Cloud settings are present on the stick at revision '+snapshot.revision+'. Account access has not been checked by this page.':
  'No complete Koofr setup is reported. Add your account and destination folder.';
 const keepLogin=snapshot.provider==='koofr'&&snapshot.has_username&&snapshot.has_password;
 $('cloud-keep-login-row').hidden=!keepLogin;$('cloud-keep-login').checked=keepLogin;
 $('cloud-keep-folder-row').hidden=!snapshot.has_folder;$('cloud-keep-folder').checked=snapshot.has_folder;
 $('cloud-card').hidden=false;
 if(!cloudWifiReady)$('cloud-message').textContent='First check and save a Wi-Fi network above. Cloud settings can then be reviewed.';
}
function paintHandoff(){
 const card=$('portal-handoff-card');card.hidden=!portalInstructions;if(!portalInstructions)return;
 $('portal-ap-name').textContent=portalInstructions.ap_ssid;$('portal-ap-password').textContent=portalInstructions.ap_password;
 $('portal-handoff-state').textContent=Date.now()<portalInstructions.until?
  'Bluetooth is closed. Sign-in help was requested; look for the temporary Wi-Fi below. Backups remain paused during this step.':
  'The requested five-minute window has ended. Return to normal Wi-Fi and reconnect to check the stick. This page cannot confirm the result.';
}
function clearPortal(){
 portal=null;portalNetwork=null;portalPrepared=null;$('portal-card').hidden=true;$('portal-ready').hidden=true;
 $('portal-network').textContent='';$('portal-message').textContent='';paintHandoff();
}
function offerPortal(saved){
 portalNetwork=null;portalPrepared=null;$('portal-ready').hidden=true;$('portal-message').textContent='';
 const profile=profiles.find(p=>p.id===saved.selected_profile&&p.enabled);
 if(portal&&profile&&saved.durable&&!saved.store_fault&&saved.wifi==='local_connected'&&
    saved.wifi_profile===profile.id&&saved.applied_revision===saved.revision){
  portalNetwork={profile:profile.id,revision:saved.revision};$('portal-network').textContent='Saved network: '+profile.ssid;
 }else $('portal-network').textContent='';
 $('portal-card').hidden=!portalNetwork;
}
const cardHint='Optional: choose your private key .txt file or owner-card.json. It is read only on this page, never uploaded or saved here. Keep the file private.';
function clearOwnerCard(){ownerCardDevice=null;$('owner-card').value='';$('owner-card-message').textContent=cardHint;}
const backupFields=['backup-session','backup-cloud','backup-last','backup-verified','backup-pending','backup-failed','backup-error'];
function clearBackup(){
 const card=$('backup-card');if(!card)return;
 card.hidden=true;for(const id of backupFields)$(id).textContent='';$('backup-read-at').textContent='';$('records-read-at').textContent='';
}
function clearActivity(){
 const card=$('activity-card');if(!card)return;
 card.hidden=true;$('activity-state').textContent='';$('activity-storage').textContent='';$('activity-list').replaceChildren();
}
function clearFileHistory(){
 const card=$('file-history-card');if(!card)return;
 card.hidden=true;$('file-history-state').textContent='';$('file-history-warning').textContent='';$('file-history-list').replaceChildren();
}
function fileTime(value){const n=BigInt(value);return n>=1767225600n&&n<=253402300799n?new Date(Number(n)*1000).toLocaleString():'Time unavailable';}
function fileSize(value){
 const n=BigInt(value);if(n<=9007199254740991n){const bytes=Number(n);return bytes>=1048576?(bytes/1048576).toLocaleString(undefined,{maximumFractionDigits:2})+' MiB':bytes>=1024?(bytes/1024).toLocaleString(undefined,{maximumFractionDigits:2})+' KiB':bytes+' bytes';}
 return n.toString()+' bytes';
}
function paintFileHistory(data){
 clearFileHistory();const card=$('file-history-card');if(!card||!data||!flow)return;
 $('file-history-state').textContent=data.loaded?'Saved receipt snapshot: '+fileTime(data.when)+'. Showing '+data.rows.length+' of '+data.matched+' matching records captured.':'No file records were captured for this setup window.';
 const warnings=[];
 if(data.limited)warnings.push('This view holds up to eight records. Waiting files appear first, followed by the latest verified copies.');
 if(!data.complete)warnings.push('The receipt listing was incomplete.');
 if(data.corrupt)warnings.push('A damaged saved record was skipped.');
 if(data.io_failed)warnings.push('Some internal records could not be read.');
 if(data.allocation_failed)warnings.push('The stick could not allocate this optional history view.');
 if(data.exhausted)warnings.push('The history snapshot revision limit was reached.');
 $('file-history-warning').textContent=warnings.join(' ');
 for(const row of data.rows){
  const item=document.createElement('li'),name=document.createElement('strong'),state=document.createElement('span'),detail=document.createElement('small');
  name.textContent=row.path;
  state.className='record-state'+(row.state===2?'':' record-state-waiting');
  state.textContent=row.state===2?'Cloud copy verified':row.state===1?'Uploaded; content verification pending':row.attempts||row.error?'Retry scheduled':'Waiting to upload';
  detail.textContent=fileSize(row.bytes)+(row.state===2?' · Verified '+fileTime(row.verified):BigInt(row.retry)?' · Retry after '+fileTime(row.retry):'');
  item.append(name,state,detail);
  if(row.error){const reason=document.createElement('small');reason.textContent=backupHelp(row.error,1,1);item.append(reason);}
  $('file-history-list').append(item);
 }
 card.hidden=false;
}
function activityMeaning(event){
 switch(event.k){
  case 1:return 'Log opened for this run. Load result code '+event.c+'.';
  case 2:return 'Backup service started. Device code '+event.c+'.';
  case 3:return event.c===0?'Scan finished. File verification must be checked in the backup record.':
    event.c===-2010?'Scan was partial or interrupted. Device code -2010.':'Scan outcome needs review. Device code '+event.c+'.';
  case 4:return 'Reported verified files in that scan: '+event.c+'. The scan may have been partial.';
  case 5:return event.c===0?'No retry was reported at this recorded moment.':'Backup retry was reported. Device code '+event.c+'.';
  case 6:return event.c===0?'The previously reported error cleared.':event.c===-2303?'Waiting for a usable network clock. Device code -2303.':
    event.c===-2302?'Wi-Fi was disconnected. Device code -2302.':'A device error was reported. Device code '+event.c+'.';
  case 7:return 'Backup service closed its work. Device code '+event.c+'.';
  case 8:return 'Connection check result code '+event.c+'. This does not prove that any file uploaded.';
 }
}
function paintActivity(data){
 clearActivity();if(!data||!flow)return;
 $('activity-state').textContent=data.events.length?data.events.length+' recent recorded event'+(data.events.length===1?'':'s')+'. This is history, not a live upload.':'No recent device activity was reported.';
 const caveats=[];
 if(!data.loaded)caveats.push('Saved log history was not loaded; these RAM facts may be incomplete.');
 if(data.corrupt)caveats.push('Stored log corruption was detected. Review the backup receipts separately.');
 if(data.io_failed)caveats.push('The log could not be read or saved. This does not change a backup receipt.');
 if(data.dirty)caveats.push('Some recent facts are still in RAM and may be lost if power stops.');
 if(data.exhausted)caveats.push('The event sequence limit was reached; new activity logging has stopped.');
 if(data.persisted)caveats.push('Log snapshot saved through event '+data.persisted+'.');
 if(!caveats.length)caveats.push('The stick reported no log-storage warning. File backups still require their verified receipts.');
 $('activity-storage').textContent=caveats.join(' ');
 for(const event of data.events){
  const item=document.createElement('li');
  const when=event.t?new Date(event.t*1000).toLocaleString():'Clock unavailable';
  item.textContent='Event '+event.s+' · '+when+' · uptime '+event.u+' ms. '+activityMeaning(event);
  $('activity-list').append(item);
 }
 $('activity-card').hidden=false;
}
function backupHelp(code,failed,pending){
 if(!code)return failed?'Some files were not verified in the reported scan. Original files stay on the USB.':
   pending?'No backup error reported. Some files still need verification.':'No backup error reported.';
 let help;
 switch(code){
  case -2303:help='Waiting for network time. The stick needs the correct clock before it can make a secure cloud connection.';break;
  case -2302:help='Wi-Fi was disconnected. The stick will try its saved networks when setup has finished.';break;
  case -2301:help='The backup service is not running. Close setup and check whether backups can resume.';break;
  case 401:help='Koofr rejected the saved login. Check your Koofr email and app password.';break;
  case 403:help='Koofr refused access to the backup folder. Check the account and folder permissions.';break;
  case 507:help='Cloud storage is full. Free space in Koofr or review the account quota.';break;
  case -2001:help='A secure cloud connection could not start. Check Wi-Fi, network time and the saved cloud settings.';break;
  case -2002:help='The cloud copy did not match the original. It must pass another content check before it can be marked verified.';break;
  case -2003:case -2105:case -2109:help='The SD card could not be read. Safely eject the drive before checking the card.';break;
  case -2101:help='A folder is nested too deeply. Move the file closer to the watched folder.';break;
  case -2102:help='The backup folder could not be opened. Check that the selected folder exists on the USB.';break;
  case -2103:help='There are too many directory entries to scan at once. Review the watched folder contents.';break;
  case -2104:help='A file or folder name is unsupported. Use a simpler name and try again.';break;
  case -2106:help='The scan reached its 128-file limit. Reduce the watched files or review that limit.';break;
  case -2107:help='A file exceeds the configured upload size limit. Review the limit or use a smaller file.';break;
  case -2108:case -2111:case -2112:help='The stick\'s internal backup records are unavailable or full. Backups cannot be confirmed until this is resolved.';break;
  case -2110:help='A backup record needed repair. A content check is required before that record can confirm a backup.';break;
  case -2113:help='There was not enough working memory to start backups. Backups stay paused until the stick can start safely.';break;
  case -2115:help='The stick could not close its backup records safely. Setup stays blocked; keep it connected and review its diagnostics.';break;
  case -2010:help='Backup paused for setup or a new USB copy. The file needs another check after setup and copying finish.';break;
  case -2011:help='The cloud folder response could not be used. Check the backup destination before retrying.';break;
  case -2012:help='The transfer did not finish. Keep the stick connected so it can retry when copying and setup have finished.';break;
  default:help='Backup needs attention. Check the stick\'s status before retrying.';
 }
 return help+' Device code: '+code+'. Original files stay on the USB.';
}
function paintBackup(data){
 clearBackup();const card=$('backup-card');if(!card||!flow)return;
 const fields=['cloud_configured','last_backup_unix','verified','pending','failed','backup_error','backups_paused'];
 if(!data||!fields.every(key=>Object.hasOwn(data,key))||typeof data.cloud_configured!=='boolean'||typeof data.backups_paused!=='boolean'||
   !Number.isSafeInteger(data.last_backup_unix)||data.last_backup_unix<0||data.last_backup_unix>253402300799||
   !['verified','pending','failed'].every(key=>Number.isInteger(data[key])&&data[key]>=0&&data[key]<=0xffffffff)||
   !Number.isInteger(data.backup_error)||data.backup_error< -0x80000000||data.backup_error>0x7fffffff)return;
 $('backup-session').textContent=data.backups_paused?'Backups are paused during this setup session.':'The stick did not confirm that backups are paused. Disconnect and check the stick before continuing.';
 $('backup-cloud').textContent=data.cloud_configured?'Saved on the stick; access is checked separately.':'Cloud setup is needed.';
 $('backup-last').textContent=data.last_backup_unix?new Date(data.last_backup_unix*1000).toLocaleString():'No verified backup record reported yet.';
 $('backup-verified').textContent=data.verified.toLocaleString();$('backup-pending').textContent=data.pending.toLocaleString();$('backup-failed').textContent=data.failed.toLocaleString();
 $('backup-pending-card').className='summary-card'+(data.pending?' tone-warning':'');$('backup-failed-card').className='summary-card'+(data.failed?' tone-error':'');
 $('backup-read-at').textContent='Read from the stick at '+new Date().toLocaleTimeString()+'. Recorded results, not a live upload.';
 $('backup-error').textContent=backupHelp(data.backup_error,data.failed,data.pending);
 card.hidden=false;
}
const controls=['scan','manual','check','save','manual-ssid','manual-open','wifi-password','keep-secret'];
const supported=()=>desktopContext.secureContext===true&&!!desktopContext.bluetooth?.requestDevice;
function clearCandidate(){
 wifiCandidatePending=false;
 selected=null;$('candidate-card').hidden=true;$('wifi-password').value='';
 $('selected-name').textContent='';$('selected-security').textContent='';
 $('wifi-password').type='password';$('show-wifi-password').checked=false;
 $('check-message').textContent='';$('save-panel').hidden=true;$('accept-local').checked=false;
}
function buttons(){
 $('records-refresh').disabled=busy||!fullyAuthenticated||!flow||!!cloudStaged||!!loginStaged||!!portalPrepared||wifiCandidatePending||wifiSaveUnknown||cloudSaveUnknown||loginOutcomeUnknown;
 const connectionLocked=busy||setupAccessPending;
 for(const id of controls)$(id).disabled=busy||!flow||!!portalPrepared||cloudStaged||loginStaged;
 $('save').disabled=busy||!flow||!!portalPrepared||cloudStaged||loginStaged||!$('accept-local').checked;
 $('cancel').disabled=busy||!flow||!!portalPrepared||cloudStaged||loginStaged;
 $('connect').disabled=connectionLocked||!!flow||!supported();$('owner-key').disabled=connectionLocked||!!flow||!supported();
 $('show-owner-key').disabled=connectionLocked||!!flow||!supported();
 $('show-wifi-password').disabled=busy||!flow||!!portalPrepared||cloudStaged||loginStaged||!selected||selected.open||$('keep-secret').checked;
 $('portal-prepare').disabled=busy||!flow||!portal||!portalNetwork||!!portalPrepared||cloudStaged||loginStaged;
 $('portal-continue').disabled=busy||!flow||!portal||!portalPrepared;
 $('portal-cancel').disabled=busy||!flow||!portal||!portalPrepared;
 $('owner-card').disabled=connectionLocked||!!flow||!supported();
 if(selected)$('wifi-password').disabled=busy||!flow||!!portalPrepared||cloudStaged||loginStaged||selected.open||$('keep-secret').checked;
 const locked=busy||!cloud||!flow||!!portalPrepared||cloudStaged||loginStaged;
 for(const id of ['cloud-keep-login','cloud-keep-folder'])$(id).disabled=locked;
 for(const id of ['cloud-email','cloud-password'])$(id).disabled=locked||$('cloud-keep-login').checked;
 $('cloud-folder').disabled=locked||$('cloud-keep-folder').checked;
 $('cloud-review').disabled=locked||!cloudWifiReady||cloudSnapshot?.store_fault===true;
 $('cloud-accept').disabled=busy||!cloudStaged||!flow||loginStaged;
 $('cloud-save').disabled=busy||!cloudStaged||!flow||loginStaged||!$('cloud-accept').checked;
 $('cloud-cancel').disabled=busy||!cloudStaged||!flow||loginStaged;
 $('cloud-finish').disabled=busy||!flow||cloudStaged||loginStaged;
 for(const id of ['login-username','login-password','show-login-password'])$(id).disabled=connectionLocked||!!flow||!supported();
 for(const id of ['new-login-username','new-login-password','new-login-confirm','owner-login-review'])$(id).disabled=busy||!flow||!ownerLogin||loginStaged||cloudStaged||!!portalPrepared;
 $('owner-login-accept').disabled=busy||!flow||!loginStaged;
 $('owner-login-save').disabled=busy||!flow||!loginStaged||!$('owner-login-accept').checked;
 $('owner-login-cancel').disabled=busy||!flow||!loginStaged;
 publishWizard();
}
function paint({state,message,info}){
 if(state!=='connected'){clearBackup();clearActivity();clearFileHistory();}
 $('connection-state').textContent=({connected:'Signed in securely',choosing:'Choose your stick',connecting:'Connecting',authenticating:'Checking your login',disconnected:'Not connected',error:'Connection needs attention',idle:'Not connected'})[state]||'Not connected';
 $('connection-message').textContent=state==='authenticating'?'Checking your secure login…':state==='connected'?'Signed in securely. Reading the stick’s settings…':message;
 if(info)$('identity').textContent=info.device_id+' · '+info.firmware;
 if(['disconnected','error','idle'].includes(state)){
  clearWizardLive();
  clearCloud();clearLogin();
  flow=null;busy=false;profiles=[];++epoch;clearCandidate();$('owner-key').value='';clearOwnerCard();
  clearPortal();
  $('owner-key').type='password';$('show-owner-key').checked=false;
  $('manual-ssid').value='';$('networks').replaceChildren();$('identity').textContent='';
  $('wifi-card').hidden=true;$('disconnect').hidden=true;
  $('saved-message').hidden=true;$('saved-message').textContent='';
 }
 buttons();
}
function close(){
 setupAccessPreparing=false;
 clearWizardLive();
 desktopContext.closeTransport?.();
 ++epoch;const old=connection;connection=null;flow=null;busy=false;
 $('owner-key').value='';$('wifi-password').value='';$('manual-ssid').value='';
 try{old?.disconnect();}finally{paint({state:'disconnected',message:loginSaved?'Your new login was saved. Reconnect using your new username and password.':'Reconnect with the current login saved on this stick when ready.'});}
}
desktopContext.closeSetup=close;
desktopContext.setSetupAccessPending=value=>{setupAccessPending=value===true;buttons();};
desktopContext.prepareSetupAccess=()=>{if(!supported()||busy||flow)return false;close();setupAccessPreparing=true;busy=true;buttons();return true;};
desktopContext.cancelSetupAccess=close;
const hints={1:'The owner session ended. Reconnect with your current login.',2:'Wait for the current operation to finish.',3:'Saved settings changed. Reconnect and read them again.',
 4:'This settings attempt has ended. Choose the network again.',5:'The setup window expired. Hold BOOT and reconnect.',6:'Settings could not be verified. Stop setup and review the device state.',
 7:'The saved-network store is full.',8:'This saved network is unavailable.',9:'Enter a new password when changing the network name or security.',
 10:'Confirm the network removal explicitly.',11:'Check this exact network before saving.',12:'The device is not ready for network changes.',13:'Reconnect to start a new settings session.',
 14:'The Wi-Fi password was rejected. Your previous saved settings are unchanged.',15:'The network is unavailable or its security is unsupported. Previous settings are unchanged.',
 16:'Wi-Fi did not provide a valid network address. Previous settings are unchanged.',17:'DNS is unavailable.',18:'This network needs an Accept page; internet is not ready.',19:'The endpoint is unreachable.'};
// A bounded local file read, not an upload. Card contents never enter a URL,
// network request, filename display, browser storage or diagnostic message.
$('owner-card').onchange=async()=>{
 const file=$('owner-card').files?.[0];$('owner-card').value='';
 if(!supported()||busy||setupAccessPending||flow||!file)return;
 const current=epoch;busy=true;buttons();$('owner-key').value='';ownerCardDevice=null;
 try{
  if(file.size<1||file.size>2048)throw new Error('card');
  let text=await file.text();if(current!==epoch)return;
  const bytes=new TextEncoder().encode(text).length;if(bytes>2048)throw new Error('card');
  let trimmed=text.trim();text='';
  if(/^[A-Za-z0-9_-]{32}$/.test(trimmed)){
    if(bytes>256)throw new Error('card');
    $('owner-key').value=trimmed;trimmed='';ownerCardDevice=null;$('login-username').value='csp-owner';$('login-password').value='';
    $('owner-card-message').textContent='Private key loaded locally. Hold BOOT for 3 seconds, release it, then find your stick. The key clears when you connect or leave this page.';
    return;
  }
  let card=JSON.parse(trimmed);trimmed='';
  const keys=['version','device','username','owner_key','synthetic','record_sha256','note'];
  if(!card||Array.isArray(card)||Object.keys(card).some(k=>!keys.includes(k))||card.version!==2||card.synthetic!==false||
    card.username!=='csp-owner'||typeof card.device!=='string'||!/^[0-9a-f]{12}$/.test(card.device)||
    typeof card.owner_key!=='string'||!/^[A-Za-z0-9_-]{32}$/.test(card.owner_key)||
    typeof card.record_sha256!=='string'||!/^[0-9a-f]{64}$/.test(card.record_sha256)||
    (card.note!==undefined&&(typeof card.note!=='string'||card.note.length>256)))throw new Error('card');
  $('owner-key').value=card.owner_key;ownerCardDevice=card.device;card=null;$('login-username').value='csp-owner';$('login-password').value='';
  $('owner-card-message').textContent='Private card loaded locally. Hold BOOT for 3 seconds, release it, then find your stick. The key clears when you connect or leave this page.';
 }catch{if(current===epoch){$('owner-key').value='';clearOwnerCard();$('owner-card-message').textContent='That file is not a supported private key file or owner card. Choose the original .txt key or owner-card.json, or enter your key above.';}}
 finally{if(current===epoch){busy=false;buttons();}}
};
$('owner-key').oninput=()=>{clearOwnerCard();$('login-username').value='csp-owner';$('login-password').value='';};
for(const id of ['login-username','login-password'])$(id).oninput=()=>{$('owner-key').value='';clearOwnerCard();};
$('show-login-password').onchange=()=>{
 if(!supported()||busy||flow)$('show-login-password').checked=false;
 $('login-password').type=$('show-login-password').checked?'text':'password';
};
$('show-owner-key').onchange=()=>{
 if(!supported()||busy||flow){$('show-owner-key').checked=false;}
 $('owner-key').type=$('show-owner-key').checked?'text':'password';
};
$('show-wifi-password').onchange=()=>{
 if(busy||!flow||!selected||selected.open||$('keep-secret').checked){$('show-wifi-password').checked=false;}
 $('wifi-password').type=$('show-wifi-password').checked?'text':'password';
};
function explain(error){return error instanceof WifiInputError?(hints[error.code]||'The settings were not accepted. Your saved revision has not been confirmed.'):'The operation ended. Reconnect to check the saved revision before trying again.';}
async function run(action){
 if(busy||!flow||cloudStaged||loginStaged)return;const current=epoch;busy=true;buttons();
 try{await action(current);}catch(error){if(current===epoch){if(wifiSaving&&error instanceof WifiInputError&&error.code!==15)wifiSaving=false;if(wifiSaving){wifiSaveUnknown=true;wifiSaving=false;}if(error instanceof WifiInputError&&[1,3,5,13].includes(error.code)){close();$('connection-message').textContent=explain(error);return;}$('check-message').textContent=explain(error);$('scan-message').textContent=explain(error);$('portal-message').textContent=explain(error);$('save-panel').hidden=true;}}
 finally{if(current===epoch){busy=false;buttons();}}
}
async function choose(row,current){
 if(portalPrepared)return;
 await flow.abort();if(current!==epoch)return;
 clearCandidate();selected=row;
 markWifiCandidate();
 $('selected-name').textContent=row.ssid;$('selected-security').textContent=row.open?'Open network':'WPA2 personal';
 const saved=profiles.find(p=>p.ssid===row.ssid&&p.open===row.open);
 selected={...row,id:saved?.id??0,priority:saved?.priority??0};
 $('keep-row').hidden=row.open||!saved?.has_secret;$('keep-secret').checked=!row.open&&!!saved?.has_secret;
 $('password-row').hidden=row.open;$('open-note').hidden=!row.open;
 $('candidate-card').hidden=false;buttons();
}
async function connectOwner({ownerKey='',username='',password='',expectedDevice=null,bluetooth=desktopContext.bluetooth,prepared=false}={}){
 let key=ownerKey;ownerKey='';
 if(!supported()||(prepared?!setupAccessPreparing||!busy:busy||setupAccessPending)){key='';username='';password='';return false;}
 if(!prepared)close();setupAccessPreparing=false;
 loginSaved=false;loginOutcomeUnknown=false;wifiSaveUnknown=false;cloudSaveUnknown=false;
 portalInstructions=null;paintHandoff();
 const current=++epoch;
 busy=true;buttons();
 try{
  const next=new ManagementConnection({bluetooth,secureContext:desktopContext.secureContext,sessionTimeoutMs:null,...(key?{ownerKey:key}:{username,password}),onState:state=>{if(current===epoch)paint(state);}});key='';username='';password='';connection=next;
  const info=await next.connect();if(current!==epoch)return;
  if(expectedDevice&&info.device_id!=='CSP-W2-'+expectedDevice){close();$('connection-message').textContent='This private card belongs to a different Cloud Stick. No Wi-Fi settings were read or changed.';return;}
  flow=new WifiSetupClient(next);profiles=await flow.profiles();const saved=await flow.status();
  if(current!==epoch)return;
  const backup=info.backup_status===true?await flow.backupStatus():null;
  if(current!==epoch)return;
  // Optional private records can require dozens of GATT exchanges. Keep the
  // setup path responsive and read these only on explicit records navigation.
  const activity=null,fileHistory=null;
  if(info.cloud_setup===true){const nextCloud=new CloudSetupClient(next);const snapshot=await nextCloud.status();
   if(current!==epoch)return;cloud=nextCloud;cloudWifiReady=saved.durable&&!saved.store_fault;paintCloud(snapshot);}
  if(current!==epoch)return;
  if(info.owner_login===true){ownerLogin=new OwnerLoginClient(next);$('owner-login-card').hidden=false;$('new-login-username').value='admin';}
  if(current!==epoch)return;$('wifi-card').hidden=false;$('disconnect').hidden=false;
  portal=info.public_wifi===true?new PublicWifiClient(next):null;offerPortal(saved);
  paintBackup(backup);
  paintActivity(activity);
  paintFileHistory(fileHistory);
  $('connection-message').textContent='Connected securely. Saved Wi-Fi settings were read. Search or enter your network.';
  $('cloud-outcome').hidden=true;$('cloud-outcome').textContent='';
  $('owner-login-outcome').hidden=true;$('owner-login-outcome').textContent='';
  const previous=profiles.find(p=>p.id===saved.selected_profile);
  if(previous){$('saved-message').hidden=false;$('saved-message').textContent=(saved.durable?'Saved Wi-Fi on this stick: ':'Previously configured Wi-Fi: ')+previous.ssid+'. Internet and Koofr access are checked separately.';}
  wifiConfigured=savedWifiState(saved,profiles);fullyAuthenticated=true;++authenticatedGeneration;
  recordCapabilities={backup:info.backup_status===true,activity:info.event_log===true,history:info.file_history===true};recordsAttempted=false;
  $('records-message').textContent='Open backup records when you want a saved snapshot. Nothing refreshes automatically.';
  return true;
 }catch(error){if(current===epoch){close();$('connection-message').textContent=error instanceof WifiInputError?explain(error):prepared?'This computer’s saved access could not sign in. Use your current username and password, or the advanced recovery-key option. No login was changed.':'Connection failed. Check your current username and password, then reopen the setup connection. Advanced recovery-key sign-in is also available.';}return false;}
 finally{key='';username='';password='';if(current===epoch){busy=false;buttons();}}
}
async function readRecordedStatus(force=false){
 if(!fullyAuthenticated||!flow||!recordCapabilities||busy||cloudStaged||loginStaged||portalPrepared||wifiCandidatePending||wifiSaveUnknown||cloudSaveUnknown||loginOutcomeUnknown)return false;
 // One automatic read per authenticated session. A failed optional read must
 // not become a retry loop through wizard state/render notifications.
 if(recordsAttempted&&!force)return false;
 if(!recordCapabilities.backup&&!recordCapabilities.activity&&!recordCapabilities.history){recordsAttempted=true;$('records-message').textContent='This firmware does not offer recorded backup status or file history. No snapshot was read.';return false;}
 recordsAttempted=true;const current=epoch,active=connection,capabilities=recordCapabilities;busy=true;buttons();
 clearBackup();clearActivity();clearFileHistory();$('records-message').textContent='Reading saved results from this authenticated session…';
 try{
  const backup=capabilities.backup?await flow.backupStatus():null;if(current!==epoch)return false;
  const activity=capabilities.activity?await new DiagnosticLogClient(active).read():null;if(current!==epoch)return false;
  const history=capabilities.history?await new FileHistoryClient(active).read():null;if(current!==epoch)return false;
  paintBackup(backup);paintActivity(activity);paintFileHistory(history);
  $('records-read-at').textContent='Read at '+new Date().toLocaleTimeString()+'. The backup dates and counts below were recorded earlier.';
  $('records-message').textContent='Saved snapshot read. Backups remain paused while connected. Use Done when finished.';return true;
 }catch{if(current===epoch){clearBackup();clearActivity();clearFileHistory();$('records-message').textContent='The records could not be read completely. No fresh results are shown. Reconnect if the setup session ended; no automatic retry was made.';}return false;}
 finally{if(current===epoch){busy=false;buttons();}}
}
desktopContext.readRecordedStatus=readRecordedStatus;
$('records-refresh').onclick=()=>readRecordedStatus(true);
desktopContext.connectSetupAccess=({ownerKey,bluetooth,expectedDevice})=>connectOwner({ownerKey,bluetooth,expectedDevice,prepared:true});
desktopContext.connectFirstOwner=({username,password,bluetooth,expectedDevice})=>connectOwner({username,password,bluetooth,expectedDevice,prepared:true});
$('connect').onclick=()=>{
 if(!supported()||busy||setupAccessPending)return false;
 const key=$('owner-key').value,username=$('login-username').value,password=$('login-password').value,expectedDevice=key?ownerCardDevice:null;
 $('owner-key').value='';$('login-password').value='';return connectOwner({ownerKey:key,username,password,expectedDevice});
};
$('login-form').onsubmit=event=>{event.preventDefault();};
$('new-login-form').onsubmit=event=>{event.preventDefault();};
$('disconnect').onclick=close;
$('scan').onclick=()=>run(async current=>{
 if(portalPrepared||cloudStaged)return;
 $('scan-message').textContent='Searching nearby Wi-Fi networks…';
 const rows=await flow.scan({cancelled:()=>current!==epoch});if(current!==epoch)return;
 $('networks').replaceChildren();
 for(const row of rows){const li=document.createElement('li'),button=document.createElement('button');button.type='button';button.className='network';
  button.disabled=!row.supported;button.append(document.createTextNode(row.ssid));const detail=document.createElement('span');
  detail.textContent=!row.supported?'Unsupported security':row.open?'Open · Internet may require an Accept page':'Password required';button.append(detail);
  button.onclick=()=>run(token=>choose(row,token));li.append(button);$('networks').append(li);}
 $('scan-message').textContent=rows.length?'Choose your network. Hidden networks can be entered below.':'No supported networks found. Try manual entry or search again later.';
});
$('manual').onclick=()=>run(async current=>{
 if(portalPrepared||cloudStaged)return;
 const ssid=$('manual-ssid').value;
 if(!ssid||new TextEncoder().encode(ssid).length>32||/[\x00-\x1f\x7f]/.test(ssid)){
  $('scan-message').textContent='Enter a Wi-Fi name of 1–32 UTF-8 bytes.';return;
 }
 await choose({ssid,open:$('manual-open').checked,supported:true},current);
});
$('keep-secret').onchange=()=>{ if(selected)markWifiCandidate();$('save-panel').hidden=true;$('accept-local').checked=false;$('wifi-password').value='';$('wifi-password').type='password';$('show-wifi-password').checked=false;buttons(); };
$('wifi-password').oninput=()=>{if(selected)markWifiCandidate();$('save-panel').hidden=true;$('accept-local').checked=false;buttons();};
$('manual-ssid').oninput=()=>{if($('manual-ssid').value)markWifiCandidate();buttons();};
$('manual-open').onchange=()=>{if($('manual-ssid').value)markWifiCandidate();buttons();};
$('check').onclick=()=>run(async current=>{
 if(portalPrepared||cloudStaged)return;
 if(!selected)return;
 markWifiCandidate();
 const password=$('wifi-password').value,keep=!selected.open&&$('keep-secret').checked;
 const bytes=new TextEncoder().encode(password).length;
 if(!selected.open&&!keep&&(bytes<8||bytes>63||/[\x00-\x1f\x7f]/.test(password))){$('check-message').textContent='Enter the 8–63 byte Wi-Fi password, or keep the saved password.';return;}
 $('save-panel').hidden=true;$('wifi-password').value='';$('check-message').textContent='Checking Wi-Fi from the dongle…';
 await flow.stage({ssid:selected.ssid,password:selected.open||keep?'':password,open:selected.open,keep,profile:selected.id,priority:selected.priority});
 await flow.check({cancelled:()=>current!==epoch,onStatus:state=>{if(current===epoch)$('check-message').textContent=state.wifi==='connecting'?'Joining Wi-Fi and waiting for an address…':'Checking the exact candidate…';}});
 if(current!==epoch)return;$('check-message').textContent='Wi-Fi joined and an address was received. Internet and Koofr remain unverified.';
 $('save-panel').hidden=false;$('accept-local').checked=false;buttons();
});
$('accept-local').onchange=buttons;
$('save').onclick=()=>run(async current=>{
 if(portalPrepared)return;
 if(!$('accept-local').checked)return;
 const name=selected?.ssid??'Your network';wifiSaving=true;const committed=await flow.save();if(current!==epoch)return;
 cloudWifiReady=true;
 clearCandidate();profiles=await flow.profiles();if(current!==epoch)return;
 if(portal){const saved=await flow.status();if(current!==epoch)return;offerPortal(saved);
  wifiConfigured=savedWifiState(saved,profiles);wifiSavedThisSession=committed.durable===true&&saved.revision===committed.revision&&wifiConfigured&&!wifiSaveUnknown;}
 wifiSaving=false;wifiSaveUnknown=wifiSaveUnknown||!wifiSavedThisSession;wifiCandidatePending=!wifiSavedThisSession;
 $('saved-message').hidden=false;$('saved-message').textContent=name+' was saved on the dongle and read back successfully. Internet and Koofr access still need a separate check.';
});
function cloudExplanation(error){
 if(error instanceof CloudInputError){
  if(error.uncertain)return 'The cloud Save outcome is uncertain. Reconnect to read the saved revision before retrying. No account or file backup is confirmed.';
  if([1,4,5,13].includes(error.code))return 'The cloud setup session ended. Reconnect with your private owner key before trying again.';
  if(error.code===3)return 'Saved settings changed. Reconnect and read the current cloud revision before editing.';
  if(error.code===6)return 'Check the email, app password and folder. Login fields allow up to 128 UTF-8 bytes; the folder allows up to 96 plain characters.';
  if(error.code===11)return 'A complete Koofr email, app password and folder are required. Existing stored values can be kept explicitly.';
  if(error.code===12)return 'First check and save Wi-Fi, then review cloud settings. The stick must safely pause backups for setup.';
  if(error.code===2)return 'Finish or cancel the current settings attempt first.';
  return 'Cloud settings were not confirmed saved. Reconnect to read the current revision before retrying.';
 }
 return 'The cloud operation ended. Its Save outcome may be unknown. Reconnect to read the current revision before retrying.';
}
async function runCloud(action){
 if(busy||!flow||!cloud||portalPrepared||loginStaged)return;const current=epoch;busy=true;buttons();
 try{await action(current);}catch(error){
  if(current!==epoch)return;
  const message=cloudExplanation(error);
  if(error instanceof CloudInputError&&!error.uncertain)cloudSaving=false;
  if(!(error instanceof CloudInputError)||error.uncertain||[1,3,4,5,13].includes(error.code)){
   close();$('cloud-outcome').hidden=false;$('cloud-outcome').textContent=message;return;
  }
  try{await cloud.abort();}catch{if(current===epoch){close();$('cloud-outcome').hidden=false;$('cloud-outcome').textContent='The staged attempt could not be closed. Reconnect to read the saved revision before retrying.';}return;}
  if(current===epoch){cloudStaged=false;$('cloud-confirm').hidden=true;$('cloud-accept').checked=false;$('cloud-message').textContent=message;}
 }finally{if(current===epoch){busy=false;buttons();}}
}
for(const id of ['cloud-email','cloud-password','cloud-folder'])$(id).oninput=()=>{$('cloud-message').textContent='';$('cloud-finish').hidden=true;};
$('cloud-keep-login').onchange=()=>{for(const id of ['cloud-email','cloud-password'])$(id).value='';$('cloud-message').textContent='';buttons();};
$('cloud-keep-folder').onchange=()=>{$('cloud-folder').value='';$('cloud-message').textContent='';buttons();};
$('cloud-accept').onchange=buttons;
$('cloud-review').onclick=()=>runCloud(async current=>{
 if(!cloudWifiReady){$('cloud-message').textContent='First check and save a Wi-Fi network above. Cloud settings can then be reviewed.';return;}
 if(cloudStaged)return;
 let fields={};
 if(!$('cloud-keep-login').checked){fields.username=$('cloud-email').value;fields.app_password=$('cloud-password').value;}
 if(!$('cloud-keep-folder').checked)fields.folder=$('cloud-folder').value;
 try{
  if(!Object.keys(fields).length){$('cloud-message').textContent='All saved cloud values are being kept. No changes need saving. Finish setup when ready.';$('cloud-finish').hidden=false;return;}
  validateCloudFields(fields);
  if(Object.hasOwn(fields,'username')&&!/^[^@\s:]+@[^@\s:]+$/.test(fields.username))throw new CloudInputError(6);
  await flow.abort();if(current!==epoch)return;clearCandidate();
  $('cloud-message').textContent='Staging your changes on the nearby stick…';
  await cloud.stage(fields);if(current!==epoch)return;
  cloudStaged=true;$('cloud-confirm').hidden=false;$('cloud-accept').checked=false;
  $('cloud-message').textContent='Ready to review. Nothing has been saved or tested. Confirm below only when these are the account and folder you want.';
 }finally{for(const field of Object.keys(fields))fields[field]='';fields=null;if(current===epoch)$('cloud-password').value='';}
});
$('cloud-save').onclick=()=>runCloud(async current=>{
 if(!cloudStaged||!$('cloud-accept').checked)return;
 cloudSaving=true;$('cloud-message').textContent='Saving the reviewed cloud settings…';
 const result=await cloud.save({acceptUnverified:true});if(current!==epoch)return;
 cloudStaged=false;$('cloud-confirm').hidden=true;$('cloud-accept').checked=false;
 for(const id of ['cloud-email','cloud-password','cloud-folder'])$(id).value='';
 const snapshot=await cloud.status();if(current!==epoch)return;
 if(snapshot.revision!==result.revision||!snapshot.configured||snapshot.provider!=='koofr')throw new CloudInputError(15,{uncertain:true});
 cloudSaving=false;cloudSavedThisSession=!cloudSaveUnknown;
 paintCloud(snapshot);
 $('cloud-message').textContent='Cloud settings were saved and read back at revision '+result.revision+'. Koofr access and file uploads are not yet verified. Finish setup to let the stick safely resume its cloud checks.';
 $('cloud-finish').hidden=false;
});
$('cloud-cancel').onclick=()=>runCloud(async current=>{await cloud.abort();if(current!==epoch)return;cloudStaged=false;$('cloud-confirm').hidden=true;$('cloud-accept').checked=false;
 for(const id of ['cloud-email','cloud-password','cloud-folder'])$(id).value='';$('cloud-message').textContent='Cloud changes cancelled. No cloud commit was requested.';});
$('cloud-finish').onclick=()=>{if(busy||cloudStaged||!flow)return;close();$('connection-message').textContent='Bluetooth is closed. Leave the USB plugged in. The stick must safely release setup before it can check cloud access and resume backups. Reconnect later to read the backup record.';};
function loginExplanation(error){
 if(error instanceof OwnerRecordError)return error.message;
 if(error instanceof OwnerLoginInputError){
  if(error.uncertain)return 'The login Save outcome is unknown. Reconnect using your new login; if it fails, use the previous login or recovery route. Do not repeat Save automatically.';
  if(error.code===2||error.code===12)return 'Another device operation is still finishing. Reconnect after it closes before changing your login.';
  if(error.code===3)return 'The saved owner record changed. Reconnect before trying a new login change.';
  if(error.code===6)return 'The new login was not accepted. Reconnect and check the input before trying again.';
  return 'The login-change session ended without a confirmed save. Reconnect before trying again.';
 }
 return 'The login operation ended without a confirmed outcome. If Save was requested, try your new login first, then the previous login or recovery route. Do not repeat Save automatically.';
}
async function runLogin(action){
 if(busy||!flow||!ownerLogin||portalPrepared||cloudStaged)return;
 const current=epoch;busy=true;buttons();
 try{await action(current);}catch(error){
  if(current!==epoch)return;
  const message=loginExplanation(error);
  if(error instanceof OwnerLoginInputError&&!error.uncertain)loginSaving=false;
  if(error instanceof OwnerRecordError){
   try{await ownerLogin.abort();}catch{if(current===epoch){close();$('owner-login-outcome').hidden=false;$('owner-login-outcome').textContent='The staged login could not be closed. Reconnect before trying again. No successful save was confirmed.';}return;}
   if(current===epoch){loginStaged=false;$('owner-login-confirm-panel').hidden=true;$('owner-login-accept').checked=false;$('owner-login-message').textContent=message;}
  }else{close();$('owner-login-outcome').hidden=false;$('owner-login-outcome').textContent=message;}
 }finally{if(current===epoch){busy=false;buttons();}}
}
$('owner-login-accept').onchange=buttons;
$('owner-login-review').onclick=()=>runLogin(async current=>{
 if(loginStaged)return;
 loginSaved=false;loginOutcomeUnknown=false;
 let username=$('new-login-username').value,password=$('new-login-password').value,confirmation=$('new-login-confirm').value;
 $('new-login-password').value='';$('new-login-confirm').value='';
 try{
  validateOwnerLogin(username,password,{newPassword:true});
  if(password!==confirmation)throw new OwnerRecordError('confirmation','The two new passwords do not match. Enter them again.');
  await flow.abort();if(current!==epoch)return;if(cloud)await cloud.abort();if(current!==epoch)return;clearCandidate();
  $('owner-login-message').textContent='Preparing the new login privately in this browser…';
  await ownerLogin.stage({username,password,cancelled:()=>current!==epoch});if(current!==epoch)return;
  loginStaged=true;$('owner-login-confirm-panel').hidden=false;$('owner-login-accept').checked=false;
  $('owner-login-message').textContent='The new login is ready to review. Your current login is still active. Nothing has been saved.';
 }finally{username='';password='';confirmation='';}
});
$('owner-login-save').onclick=()=>runLogin(async current=>{
 if(!loginStaged||!$('owner-login-accept').checked)return;
 loginSaving=true;$('owner-login-message').textContent='Saving your new login…';
 await ownerLogin.save({accept:true});if(current!==epoch)return;
 loginSaving=false;loginStaged=false;loginSaved=true;loginOutcomeUnknown=false;close();
 $('owner-login-outcome').hidden=false;$('owner-login-outcome').textContent='Your new login was saved. Bluetooth is closed. Reconnect using the username and password you chose; the previous login or private key has been replaced.';
});
$('owner-login-cancel').onclick=()=>runLogin(async current=>{
 await ownerLogin.abort();if(current!==epoch)return;loginStaged=false;$('owner-login-confirm-panel').hidden=true;$('owner-login-accept').checked=false;
 for(const id of ['new-login-password','new-login-confirm'])$(id).value='';
 $('owner-login-message').textContent='Login change cancelled. No replacement login was committed.';
});
$('portal-prepare').onclick=()=>run(async current=>{
 if(!portal||!portalNetwork||portalPrepared)return;
 $('portal-message').textContent='Preparing private sign-in instructions…';
 const prepared=await portal.prepare(portalNetwork);if(current!==epoch)return;
 portalPrepared={...prepared,until:Date.now()+prepared.handoff_ms};$('portal-ready').hidden=false;
 $('portal-message').textContent='Ready. Tap Continue to close Bluetooth and start the temporary connection. No venue terms have been accepted.';
});
$('portal-cancel').onclick=()=>run(async current=>{
 if(!portal)return;await portal.cancel();if(current!==epoch)return;
 portalPrepared=null;$('portal-ready').hidden=true;$('portal-message').textContent='Sign-in help cancelled. No temporary Wi-Fi was requested.';
});
$('portal-continue').onclick=()=>run(async current=>{
 if(!portal||!portalPrepared)return;
 if(Date.now()>=portalPrepared.until){await portal.cancel();if(current===epoch){portalPrepared=null;$('portal-ready').hidden=true;$('portal-message').textContent='The instructions expired. Prepare sign-in help again.';}return;}
 portalPrepared=null;$('portal-ready').hidden=true;
 const instructions=await portal.handoff();if(current!==epoch)return;
 // The authenticated ACK is completely read before deliberate disconnection.
 // Retain only public, short-lived AP instructions across phone Settings/page
 // backgrounding; identity, owner key, grants and saved-network facts clear.
 close();portalInstructions={...instructions,until:Date.now()+instructions.window_seconds*1000};paintHandoff();
 globalThis.setTimeout?.(()=>paintHandoff(),instructions.window_seconds*1000+20);
});
$('cancel').onclick=()=>{if(portalPrepared)return;if(busy){close();return;}run(async current=>{await flow.abort();if(current===epoch){clearCandidate();$('scan-message').textContent='Changes cancelled. Previous saved settings remain selected.';}});};
addEventListener('pagehide',close);addEventListener('pageshow',event=>{if(event.persisted)close();});
document.addEventListener('visibilitychange',()=>{if(document.visibilityState!=='visible')close();});
document.addEventListener('freeze',close);document.addEventListener('resume',close);
if(!supported())$('connection-message').textContent='The local secure connection is unavailable. Check the desktop app installation; login entry stays disabled.';
else $('connection-message').textContent='Enter your login or use the existing private-key option, open the physical setup window, then find your stick.';
buttons();
