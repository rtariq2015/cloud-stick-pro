/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Cloud Stick Pro contributors
 */
import {NativeRPC} from './native.mjs';
const flags=['wifi','media','active','scanning','has_scan','scan_complete','window_open','paused','cloud','configured'];
const numbers=['uptime_ms','verified','pending','failed','bytes_per_second','quiet_seconds','last_backup_unix','error','percent','phase'];
export function validStatus(s){
 if(!s||typeof s!=='object'||Object.keys(s).sort().join(',')!==[...flags,...numbers,'firmware','done','total'].sort().join(','))return false;
 if(typeof s.firmware!=='string'||!/^[A-Za-z0-9.-]{1,48}$/.test(s.firmware)||flags.some(k=>typeof s[k]!=='boolean'))return false;
 if(numbers.some(k=>!Number.isSafeInteger(s[k])))return false;
 if(numbers.filter(k=>!['error','percent'].includes(k)).some(k=>s[k]<0)||s.percent< -1||s.percent>100||s.phase>4)return false;
 return ['done','total'].every(k=>typeof s[k]==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(s[k])&&BigInt(s[k])<=18446744073709551615n);
}
export function dashboardView(s){
 if(!validStatus(s))throw Error('dashboard_invalid');
 const transferring=s.active&&s.phase>=1&&s.phase<=4,scanning=s.active&&s.scanning;
 const phases=['Waiting','Checking a USB file','Uploading to the cloud','Verifying the cloud copy','Publishing the verified copy'];
 let title='Ready for new files',detail='Leave your stick powered. New files are backed up after your saved quiet period.';
 if(!s.media){title='Insert your microSD';detail='The stick cannot use its storage. Saved counts do not mean the files are currently available.';}
 else if(!s.wifi){title='Waiting for Wi-Fi';detail='The stick is trying its saved connections. Your original files stay on the microSD.';}
 else if(!s.configured){title='Finish cloud setup';detail='Open Cloud Stick Setup on your computer to review the cloud destination.';}
 else if(transferring){title=phases[s.phase];detail=s.phase===1?'Identifying the local file before upload.':s.phase===3?'Reading the cloud copy back and comparing its contents.':s.phase===4?'Finishing the checked copy before recording success.':'Your original stays on the stick. The cloud copy is checked before success is recorded.';}
 else if(s.error){title=s.error===-2303?'Waiting for network time':'Backup needs attention';detail=s.error===-2303?'The stick needs the correct time before making a secure cloud connection.':'Device code '+s.error+'. Original files remain on the stick; recoverable errors are retried.';}
 else if(s.paused){title='Backups paused';detail='Setup or maintenance needs to finish before backup work can continue.';}
 else if(!s.window_open){title='Waiting for your schedule';detail='Backup work can continue during your saved time window.';}
 else if(scanning){title='Finding new or changed files';detail='Comparing files with saved records. This is not a file upload.';}
 else if(s.active){title='Preparing the next backup';detail='The backup worker is active; a file transfer has not started.';}
 else if(s.quiet_seconds){title='Waiting for copying to settle';detail='Backup can start in '+s.quiet_seconds+' seconds if file activity stays quiet.';}
 else if(s.scan_complete&&s.verified&&!s.pending&&!s.failed){title='Your files are verified';detail='The latest completed scan found no files waiting for backup.';}
 else if(s.failed||s.pending){title='Waiting to retry backup';detail='Some files still need a verified cloud copy. Their originals remain on the stick.';}
 else if(s.has_scan&&!s.scan_complete){title='Waiting to continue';detail='The previous scan did not finish. Its counts cover only part of the work.';}
 const scan=!s.has_scan?'Waiting for the first scan.':scanning?'Scan in progress; counts can change.':s.scan_complete?'Latest completed scan.':'Partial counts from an incomplete scan.';
 return {title,detail,scan,transferring,scanning,phase:phases[s.phase],progress:transferring&&s.phase!==4&&s.percent>=0?s.percent:null};
}
const size=value=>{const n=Number(value);return n>=1048576?(n/1048576).toFixed(1)+' MiB':n>=1024?(n/1024).toFixed(1)+' KiB':n+' B';};
export function createApplication({document=globalThis.document,rpc=new NativeRPC(),viewerFactory,setTimer=setTimeout,clearTimer=clearTimeout,now=()=>new Date()}={}){
 const $=id=>document.getElementById(id),viewer=viewerFactory({document,rpc});
 let mode='dashboard',address='',epoch=0,timer=null,inflight=false,switching=false,failures=0,initialized=false,addressBusy=false;
 const text=(id,value)=>{$(id).textContent=value;};
 function stale(message='Reconnect to the same Wi-Fi as your stick. If its IP address changed, update it below.'){
  text('dashboard-badge','Not connected');text('live-title',address?'Reconnecting to your stick':'Your backups, at a glance');
  text('live-detail',address?message:'Add the IP address shown on your stick. No login or Bluetooth needed for live status.');
  text('live-updated',address?'No live connection. Any displayed counts are from the previous read.':'Add your stick to view live activity.');
  text('live-scan',address?'Previous counts; a current scan has not been confirmed.':'Counts appear after the first connection.');
  text('live-transfer','No current transfer information');text('live-speed','—');$('live-progress').hidden=true;$('live-progress').removeAttribute('value');
 }
 function clear(){for(const id of ['live-verified','live-pending','live-failed','live-wifi','live-media','live-firmware','live-last'])text(id,'—');}
 function stop(){++epoch;clearTimer(timer);timer=null;inflight=false;void rpc.request('dashboard_cancel').catch(()=>{});}
 function render(s){
  const view=dashboardView(s);text('dashboard-badge','Live over Wi-Fi');text('live-title',view.title);text('live-detail',view.detail);text('live-scan',view.scan);
  for(const key of ['verified','pending','failed'])text('live-'+key,s.has_scan?s[key].toLocaleString():'—');
  text('live-wifi',s.wifi?'Connected':'Reconnecting');text('live-media',s.media?'Ready':'Not ready');text('live-firmware',s.firmware);
  text('live-last',s.last_backup_unix?new Date(s.last_backup_unix*1000).toLocaleString():'No verified backup recorded yet');
  text('live-transfer',view.transferring?s.phase===4?'Finishing the checked copy':size(s.done)+' / '+size(s.total):view.scanning?'Comparing file records':s.active?'Preparing backup':'No transfer running');
  text('live-speed',view.transferring&&s.phase!==4?size(s.bytes_per_second)+'/s':'—');
  $('live-progress').hidden=!view.transferring&&!view.scanning;
  if(view.progress===null)$('live-progress').removeAttribute('value');else $('live-progress').value=view.progress;
  text('live-updated','Read from your stick at '+now().toLocaleTimeString()+'. Backup counts describe its reported scan.');
 }
 async function poll(){
  if(document.hidden||mode!=='dashboard'||!address||inflight||switching)return;
  clearTimer(timer);const token=epoch;inflight=true;$('live-refresh').disabled=true;
  try{const status=await rpc.request('dashboard_status');if(token!==epoch||document.hidden||mode!=='dashboard')return;render(status);failures=0;}
  catch{if(token===epoch&&mode==='dashboard'&&!document.hidden){failures++;stale();}}
  finally{if(token===epoch){inflight=false;$('live-refresh').disabled=false;if(!document.hidden&&mode==='dashboard'&&address)timer=setTimer(poll,Math.min(15000,3000*Math.max(1,failures)));}}
 }
 function showAddress(){$('dashboard-data').hidden=!address;$('live-refresh').disabled=!address;text('saved-address',address||'No stick added yet');$('dashboard-address').value=address;$('address-details').open=!address;$('forget-address').hidden=!address;}
 async function switchMode(next){
  if(switching||mode===next)return;switching=true;$('dashboard-tab').disabled=true;$('private-tab').disabled=true;
  stop();
  try{
   // Wait for native close acknowledgement before returning to normal Wi-Fi monitoring.
   if(next==='dashboard'&&await viewer.close('Returning to your Wi-Fi dashboard.')!==true)return;
   mode=next;if(mode==='private')text('dashboard-badge','Private history');$('dashboard-pane').hidden=mode!=='dashboard';$('private-pane').hidden=mode!=='private';
   $('dashboard-tab').setAttribute('aria-selected',String(mode==='dashboard'));$('private-tab').setAttribute('aria-selected',String(mode==='private'));
   if(mode==='dashboard'){stale();$('live-refresh').disabled=false;}
  }finally{switching=false;$('dashboard-tab').disabled=false;$('private-tab').disabled=false;}
  if(mode==='dashboard')void poll();
 }
 $('dashboard-tab').addEventListener('click',()=>switchMode('dashboard'));
 $('private-tab').addEventListener('click',()=>switchMode('private'));
 $('live-refresh').addEventListener('click',()=>poll());
 $('address-form').addEventListener('submit',async event=>{
  event.preventDefault();if(switching||addressBusy)return;addressBusy=true;const previous=address;stop();clear();address='';$('save-address').disabled=true;
  try{const result=await rpc.request('dashboard_save',{address:$('dashboard-address').value.trim()});address=result.address;initialized=true;showAddress();failures=0;text('address-message','Address saved on this phone.');stale();}
  catch{address=previous;showAddress();stale();$('address-details').open=true;text('address-message','Enter the local IP shown on your stick, for example 192.168.1.42. Websites, passwords and other addresses are not accepted.');}
  finally{addressBusy=false;$('save-address').disabled=false;$('live-refresh').disabled=false;}
  void poll();
 });
 $('forget-address').addEventListener('click',async()=>{
  if(addressBusy)return;addressBusy=true;const previous=address;stop();clear();address='';stale();$('forget-address').disabled=true;
  try{await rpc.request('dashboard_save',{address:''});initialized=true;showAddress();text('address-message','Saved address removed.');}
  catch{address=previous;showAddress();text('address-message','The address could not be removed. Try again.');void poll();}
  finally{addressBusy=false;$('forget-address').disabled=false;}
 });
 async function loadAddress(){
  const token=epoch;
  try{const result=await rpc.request('dashboard_address');if(token!==epoch)return;address=result.address;initialized=true;showAddress();stale();if(address)await poll();}
  catch{if(token===epoch)stale('The saved address could not be read. Close and reopen the app, or add the address again.');}
 }
 document.addEventListener('visibilitychange',()=>{stop();if(mode==='dashboard'){stale();$('live-refresh').disabled=false;if(!document.hidden){if(initialized)void poll();else void loadAddress();}}});
 const ready=loadAddress();
 return {ready,stop,switchMode};
}
