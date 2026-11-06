import {ManagementConnection} from './connection.mjs';
const $=id=>document.getElementById(id);
const labels={idle:'Not connected',choosing:'Choose device',connecting:'Connecting',authenticating:'Checking ownership',connected:'Secure test session',disconnected:'Disconnected',error:'Needs attention'};
function paint({state,message,info}){
 const busy=['choosing','connecting','authenticating'].includes(state),connected=state==='connected';
 $('state-pill').textContent=labels[state]||'Not connected';$('state-pill').className='pill '+(connected?'connected':state==='error'?'error':'');
 $('state-message').textContent=message;$('connect').hidden=connected;$('connect').disabled=busy;$('session-actions').hidden=!connected;
 $('connection-title').textContent=connected?'You’re connected':state==='error'?'Let’s try that again':'Find your Cloud Stick';
 $('device-card').hidden=!connected;
 if(!connected)for(const id of ['device-title','device-id','firmware','wifi','backups'])$(id).textContent='';
 if(info){$('device-title').textContent=info.model;$('device-id').textContent=info.device_id;$('firmware').textContent=info.firmware;$('wifi').textContent=info.wifi_connected?'Connected':'Not connected';$('backups').textContent=info.backups_paused?'Paused for Bluetooth setup':'Status unavailable';}
}
const preview=new URLSearchParams(location.search).get('preview');
if(preview){
 $('preview-banner').hidden=false;$('connect').disabled=true;
 if(preview==='connected')paint({state:'connected',message:'Simulated preview only. No Bluetooth connection has been made.',info:{model:'LILYGO T-Dongle S3',device_id:'CSP-M1-PREVIEW',firmware:'0.3.3-dev · M1 prototype',wifi_connected:true,backups_paused:true}});
 else if(preview==='error')paint({state:'error',message:'Simulated preview: the ownership check failed. Reopen the physical setup window and try again.'});
 $('connect').disabled=true;$('refresh').disabled=true;$('disconnect').disabled=true;
}else{
 const connection=new ManagementConnection({onState:paint});
 // A secure badge represents only this foreground session. Browser timers can
 // pause while hidden/frozen; restoring a page never restores authentication.
 let frozen=false;
 const showUnsupported=()=>{if(!isSecureContext||!navigator.bluetooth)paint({state:'error',message:'Open this page over trusted HTTPS in Android Chrome with Bluetooth enabled. This computer can still show a visual preview.'});};
 const endSession=()=>{connection.disconnect();showUnsupported();};
 const foreground=action=>{if(frozen||document.visibilityState!=='visible'){endSession();return Promise.resolve();}return action().catch(()=>{});};
 $('connect').onclick=()=>foreground(()=>connection.connect());
 $('refresh').onclick=()=>foreground(()=>connection.refresh());
 $('disconnect').onclick=endSession;
 addEventListener('pagehide',endSession);
 addEventListener('pageshow',()=>{frozen=false;endSession();});
 document.addEventListener('visibilitychange',()=>{if(document.visibilityState!=='visible')endSession();});
 document.addEventListener('freeze',()=>{frozen=true;endSession();});
 document.addEventListener('resume',()=>{frozen=false;endSession();});
 showUnsupported();
}
