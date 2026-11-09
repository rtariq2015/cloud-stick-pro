import {ManagementConnection,WifiSetupClient,FileHistoryClient} from './shared/connection.mjs';
import {NativeRPC,NativeBluetooth} from './native.mjs';
import {mountSettings} from './settings.mjs';
import {createApplication} from './dashboard.mjs';
const date=value=>Number(value)>0?new Date(Number(value)*1000).toLocaleString():'No recorded time';
export function readableSize(value){
 try{const n=BigInt(value);if(n<0n)throw Error();if(n>9007199254740991n)return n.toLocaleString()+' bytes';const bytes=Number(n);for(const [scale,label]of [[1073741824,'GiB'],[1048576,'MiB'],[1024,'KiB']])if(bytes>=scale)return (bytes/scale).toLocaleString(undefined,{maximumFractionDigits:2})+' '+label;return bytes.toLocaleString()+' bytes';}catch{return 'Size unavailable';}
}
export function viewerHelp(error,phase='connecting'){
 const code=typeof error?.code==='string'?error.code:typeof error?.message==='string'?error.message:'';
 if(code.startsWith('wake_'))return code==='wake_busy'?'Your stick is busy. Let its current work finish and try again.':'The button-free connection could not finish. Check the saved IP address and Wi-Fi, or choose the BOOT option below for this attempt.';
 if(['bluetooth_permission_denied','bluetooth_permission_required'].includes(code))return 'Nearby devices access is needed. Allow it for this app in Android settings, then try again.';
 if(['bluetooth_unavailable','bluetooth_scan_failed'].includes(code))return 'Check that Bluetooth is on. Keep the stick nearby, open its setup window and try again.';
 if(code==='bluetooth_cleanup_failed')return 'The previous Bluetooth link could not be confirmed closed. Close and reopen this app before another attempt.';
 if(['bluetooth_mtu_failed','bluetooth_service_mismatch'].includes(code))return 'This Bluetooth connection does not offer the expected protocol. Check the installed firmware with Cloud Stick Setup.';
 if(code==='unsupported_status')return 'This firmware does not offer recorded backup status. Check its version with Cloud Stick Setup.';
 if(error?.name==='NotFoundError')return 'No stick selected. Hold BOOT for 3 seconds, release it, then choose your own stick from the nearby list.';
 if(phase==='permission')return 'Nearby devices permission was not completed. Allow access when Android asks, then try again.';
 if(phase==='authenticating')return 'Your login could not be verified. Use the current personal username and password from Cloud Stick Setup, and reopen the stick’s setup window.';
 if(phase==='records')return 'The saved records could not be read completely. No new results are shown. Reopen the setup window and sign in to check again.';
 return 'The connection ended. Keep the stick powered and nearby, hold BOOT for 3 seconds, then try again.';
}
export function backupAttention(status){
 const errors={401:'The saved report says Koofr rejected its login. Review the Koofr app password in Cloud Stick Setup.',403:'The saved report says Koofr refused folder access. Review the account and folder permissions.',507:'The saved report says cloud storage is full. Review the available space.',[-2302]:'Wi-Fi was disconnected when this result was recorded.',[-2002]:'A cloud copy did not match its content check. It has not been marked verified.'};
 if(status.backup_error)return (errors[status.backup_error]||'The saved backup report needs attention.')+' Device code: '+status.backup_error+'.';
 if(status.failed)return 'Some file versions were not verified in this report. Original files stay on the USB.';
 if(status.pending)return 'Some file versions still need verification. Backups are paused during this check.';
 return 'No backup error in this saved report. Counts may cover an earlier or partial scan.';
}
export function createViewer({document=globalThis.document,rpc=new NativeRPC(),bluetoothFactory=(rpc,choose)=>new NativeBluetooth(rpc,choose),connectionFactory=options=>new ManagementConnection(options)}={}){
const $=id=>document.getElementById(id);
let connection=null,epoch=0,select=null,busy=false,permissionWaiting=false,closing=false,phase='connecting';
const bluetooth=bluetoothFactory(rpc,devices=>new Promise(resolve=>{
 select=resolve;$('chooser').hidden=false;$('devices').replaceChildren();$('connection-badge').textContent='Choose your stick';
 for(const device of devices){const button=document.createElement('button');button.type='button';button.textContent=`${device.name} · ending ${device.id.slice(-5)}`;button.addEventListener('click',()=>{select=null;$('chooser').hidden=true;resolve(device.id);},{once:true});$('devices').append(button);}
}));
const message=text=>{$('message').textContent=text;};
const managerTabs=[...document.querySelectorAll?.('[data-manager-view]')??[]].filter(node=>node.tagName==='BUTTON');
function managerView(view='overview'){
 if(document.body)document.body.dataset.managerView=view;
 for(const tab of managerTabs)tab.setAttribute('aria-selected',String(tab.dataset.managerView===view));
}
for(const tab of managerTabs)tab.addEventListener('click',()=>managerView(tab.dataset.managerView));

const controls=value=>{busy=value;$('connect').disabled=value;$('refresh').disabled=value;$('done').disabled=closing;$('trust-phone').disabled=value;settings?.refreshLocks();};
const settings=$('stick-settings')?mountSettings({document,connection:()=>connection,lock:controls,message,isBusy:()=>busy}):null;
function close(text='This check is finished.'){
 settings?.clear();managerView();
 const token=++epoch,current=connection;connection=null;current?.disconnect();select?.(null);select=null;$('chooser').hidden=true;
 $('password').value='';$('trust-pairing').hidden=true;$('pairing-message').textContent='';$('result').hidden=true;$('login').hidden=false;$('status').replaceChildren();$('history').replaceChildren();$('snapshot-read').textContent='';$('backup-attention').textContent='';
 for(const name of ['verified','pending','failed'])$('count-'+name).textContent='—';
 closing=true;controls(true);$('session-actions').hidden=false;$('connection-badge').textContent='Closing Bluetooth';message(text+' Closing the app’s Bluetooth link…');
 // Hide private results immediately, but never infer radio/worker state from
 // clearing JS authentication. Only the native close promise confirms its link.
 return Promise.resolve().then(()=>bluetooth.close()).then(()=>{
  if(token!==epoch)return;closing=false;controls(false);$('session-actions').hidden=true;$('connection-badge').textContent='Bluetooth closed';
  message(text+' The app confirmed its Bluetooth link closed. Leave the USB plugged in; this does not confirm that backups have resumed.');return true;
 }).catch(()=>{
  if(token!==epoch)return;closing=true;controls(true);$('session-actions').hidden=true;$('connection-badge').textContent='Close not confirmed';
  message(text+' The app could not confirm Bluetooth closed. Close and reopen this app before another check. Backup resumption is not confirmed.');return false;
 });
}
globalThis.cloudStickNativeEvent=event=>{rpc.event(event);if(event?.type==='disconnected'&&connection)void close('Session ended. Sign in again when you need another check.');};
document.addEventListener('visibilitychange',()=>{if(document.hidden&&!permissionWaiting)void close('Session closed while the app was in the background.');});
$('cancel').addEventListener('click',()=>close('Selection cancelled.'));
$('done').addEventListener('click',()=>close());
async function read(token,initialInfo){
 // connect() already returns validated authenticated info. Reuse it once;
 // an explicit Refresh still performs a fresh read, without periodic polling.
 const current=connection,info=initialInfo??await current.refresh();if(token!==epoch)throw new Error('cancelled');phase='records';
 if(!info.backup_status)throw new Error('unsupported_status');
 const status=await new WifiSetupClient(current).backupStatus();if(token!==epoch)throw new Error('cancelled');
 const history=info.file_history?await new FileHistoryClient(current).read():null;if(token!==epoch)throw new Error('cancelled');
 $('snapshot-read').textContent='Read from your stick at '+new Date().toLocaleTimeString()+'. The results below were recorded earlier; this did not test the cloud.';
 for(const name of ['verified','pending','failed'])$('count-'+name).textContent=status[name].toLocaleString();
 $('card-pending').className='summary-card'+(status.pending?' tone-warning':'');$('card-failed').className='summary-card'+(status.failed?' tone-error':'');
 $('backup-attention').textContent=backupAttention(status);
 const fields=[['Last recorded backup',date(status.last_backup_unix)],['Cloud configuration',status.cloud_configured?'Saved; access not tested here':'Not saved'],['Wi-Fi now',info.wifi_connected?'Connected locally':'Not connected'],['Firmware',info.firmware],['Backups during this check','Paused']];
 $('status').replaceChildren();for(const [label,value]of fields){const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=String(value);$('status').append(dt,dd);}
 $('history').replaceChildren();const warnings=[];
 if(history){if(!history.complete)warnings.push('This receipt listing is incomplete.');if(history.limited)warnings.push('More saved records exist.');if(history.corrupt||history.io_failed)warnings.push('Some saved records were damaged or unreadable.');if(history.allocation_failed||history.exhausted)warnings.push('The stick could not prepare the complete optional view.');}
 $('history-note').textContent=history?'Showing '+history.rows.length+' of '+history.matched+' matching saved records. Up to 8 are kept here; this is not a full file list. Snapshot: '+date(history.when)+'. '+warnings.join(' '):'Saved file records are unavailable in this firmware.';
 if(history&&!history.rows.length){const empty=document.createElement('li');empty.textContent='No saved file records in this snapshot. New USB copies can appear after a later scan.';$('history').append(empty);}
 for(const row of history?.rows??[]){const li=document.createElement('li'),name=document.createElement('strong'),state=document.createElement('span'),details=document.createElement('small');name.textContent=row.path;state.className='file-state'+(row.state===2?'':' tone-warning');state.textContent=row.state===2?'Cloud copy verified':row.state===1?'Uploaded · verification pending':row.attempts||row.error?'Retry scheduled':'Waiting to upload';details.textContent=readableSize(row.bytes)+(row.state===2?' · Verified '+date(row.verified):Number(row.retry)>0?' · Retry after '+date(row.retry):'')+(row.error?' · Device code '+row.error:'');li.append(name,state,details);$('history').append(li);}
 $('trust-pairing').hidden=info.ble_wake!==true;
 $('login').hidden=true;$('result').hidden=false;$('session-actions').hidden=false;$('connection-badge').textContent='Connected · backups paused';message('Saved results read. Tap Done when you are finished.');
}
$('login-form').addEventListener('submit',async event=>{
 event.preventDefault();if(busy||closing)return;const token=++epoch;let connecting=true;controls(true);phase='permission';$('connection-badge').textContent='Nearby devices permission';
 let username=$('username').value,password=$('password').value;$('password').value='';
 try{
  permissionWaiting=true;try{await rpc.request('permission');}finally{permissionWaiting=false;}if(token!==epoch)return;
  await bluetooth.drain();if(token!==epoch)return;
  bluetooth.expectDevice?.(null);phase='waking';
  if(!$('manual-connect').checked){
   message('Asking your trusted stick to open Bluetooth over Wi-Fi…');const wake=await rpc.request('wake');if(token!==epoch)return;
   if(wake.state==='opening'&&Object.keys(wake).sort().join(',')==='device_id,state'&&/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(wake.device_id)&&typeof bluetooth.expectDevice==='function'){
    bluetooth.expectDevice(wake.device_id);message('Your trusted stick is pausing safely. Waiting for its Bluetooth window…');
   }else if(wake.state==='first_pair'&&Object.keys(wake).length===1)message('First connection: hold BOOT for 3 seconds, release, then choose your stick. After signing in, tap Trust this phone.');
   else throw new Error('wake_invalid_response');
  }else message('Hold BOOT for 3 seconds, release, then choose your stick.');
  phase='connecting';
  connection=connectionFactory({bluetooth,secureContext:true,username,password,sessionTimeoutMs:null,
   onState:state=>{if(token!==epoch)return;if(connecting&&state.state==='error')return;if(['disconnected','error'].includes(state.state)){void close(viewerHelp(null,phase));return;}if(state.state==='authenticating')phase='authenticating';$('connection-badge').textContent=state.state==='authenticating'?'Checking your login':state.state==='connected'?'Reading saved results':'Connecting';message(state.state==='authenticating'?'Checking your personal login…':state.state==='connected'?'Signed in securely. Reading saved results…':'Keep the stick powered and nearby.');}});
  $('session-actions').hidden=false;username='';password='';const info=await connection.connect();connecting=false;if(token!==epoch)return;
  await read(token,info);if(token===epoch)settings?.show();
 }catch(error){if(token===epoch)void close(viewerHelp(error,phase));}
 finally{username='';password='';if(token===epoch)controls(false);}
});
$('trust-phone').addEventListener('click',async()=>{
 if(busy||closing||!connection||$('trust-pairing').hidden)return;const token=epoch,current=connection;controls(true);
 $('pairing-message').textContent='Saving this phone’s permission on your stick…';
 try{
  const key=await rpc.request('wake_key');if(token!==epoch)return;
  if(Object.keys(key).length!==1||typeof key.public_key!=='string'||!/^04[0-9a-f]{128}$/.test(key.public_key))throw new Error('wake_invalid_response');
  const result=await current.command('wake_pair',{key:key.public_key});if(token!==epoch)return;
  if(!result||Object.keys(result).sort().join(',')!=='durable,ok,wake_id'||result.ok!==true||result.durable!==true||typeof result.wake_id!=='string'||!/^[0-9a-f]{32}$/.test(result.wake_id))throw new Error('wake_pair_failed');
  await rpc.request('wake_remember',{wake_id:result.wake_id});if(token!==epoch)return;
  $('pairing-message').textContent='This phone is trusted. Next time, keep both devices on reachable Wi-Fi and sign in here. BOOT is not needed for that connection.';
 }catch{if(token===epoch)$('pairing-message').textContent='Trust was not confirmed on both devices. No automatic retry was sent. Reconnect using BOOT, sign in and try Trust this phone again.';}
 finally{if(token===epoch)controls(false);}
});
$('refresh').addEventListener('click',async()=>{if(busy||!connection||closing)return;const token=epoch;controls(true);phase='records';message('Reading a new saved snapshot…');try{await read(token);}catch(error){if(token===epoch)void close(viewerHelp(error,phase));}finally{if(token===epoch)controls(false);}});
return {close};
}
if(typeof document!=='undefined')createApplication({viewerFactory:createViewer});
