// SPDX-License-Identifier: MIT
// Idle-only discovery. Listing metadata never authorizes an install or a write.
export function inventory(rows){
 if(!Array.isArray(rows)||rows.length>64||rows.some(r=>!r||typeof r.port!=='string'||!r.port||r.port.length>256||typeof r.label!=='string'||r.label.length>256||/[\x00-\x1f\x7f]/.test(r.label)))throw Error('invalid_inventory');
 if(new Set(rows.map(r=>r.port)).size!==rows.length)throw Error('invalid_inventory');
 if(rows.some(r=>r.token!==undefined&&(typeof r.token!=='string'||!/^[0-9a-f]{16}$/.test(r.token))))throw Error('invalid_inventory');
 return rows.map(r=>({port:r.port,label:r.label,...(r.token===undefined?{}:{token:r.token})})).sort((a,b)=>a.port.localeCompare(b.port));
}
export function watchUsb({request,idle,changed,setTimer=setTimeout,clearTimer=clearTimeout}){
 let key=null,timer=null,stopped=false,pending=false;
 async function poll(){
  if(stopped||pending)return;pending=true;
  try{if(idle()){
   const rows=inventory(await request('usb_watch_ports')),next=JSON.stringify(rows);
   if(!stopped&&idle()&&next!==key){key=next;await changed(rows);}
  }}catch{}finally{pending=false;if(!stopped)timer=setTimer(poll,3000);}
 }
 return {start:poll,stop(){stopped=true;clearTimer(timer);}};
}
export function mountAppUpdates({document,request,idle,setTimer=setTimeout,clearTimer=clearTimeout}){
 const $=id=>document.getElementById(id);let timer=null,stopped=false,pending=false,phase='idle',last=null;
 const messages={idle:'Check for a newer computer app.',checking:'Checking the official signed release…',available:'A newer app is available.',current:'You have the latest offered app.',downloading:'Downloading and checking the app…',ready:'Download checked. Ready to install.',failed:'Could not verify the update. Your current app is unchanged. Check your internet connection and try again.'};
 function render(s){
  if(!s||!Object.hasOwn(messages,s.phase)||typeof s.current!=='string'||typeof s.version!=='string'||!Number.isSafeInteger(s.done)||!Number.isSafeInteger(s.total))throw Error('invalid_update_state');
  last=s;phase=s.phase;$('app-update-message').textContent=messages[phase]+(s.version?' Offered: '+s.version+'.':'');
  $('app-update-current').textContent='App '+s.current+(s.phase==='available'?' · Update available':'');
  $('app-update-check').disabled=pending||!idle()||['checking','downloading'].includes(phase);
  $('app-update-download').hidden=phase!=='available';$('app-update-download').disabled=pending||!idle();
  $('app-update-install').hidden=phase!=='ready';$('app-update-install').disabled=pending||!idle();
  $('app-update-progress').hidden=phase!=='downloading';$('app-update-progress').max=s.total;$('app-update-progress').value=s.done;
 }
 async function poll(){if(stopped||document.hidden)return;try{render(await request('app_update_state'));}catch{}if(!stopped&&['checking','downloading'].includes(phase))timer=setTimer(poll,2500);}
 async function action(op){if(pending||!idle()||stopped)return;pending=true;try{const s=await request(op);if(!s.cancelled&&!s.installing)render(s);}catch{$('app-update-message').textContent='Finish your current setup or USB operation, then try again.';}finally{pending=false;if(last)render(last);clearTimer(timer);if(['checking','downloading'].includes(phase))timer=setTimer(poll,2500);}}
 document.addEventListener('visibilitychange',()=>{clearTimer(timer);if(!document.hidden&&['checking','downloading'].includes(phase))void poll();});
 for(const [id,op]of [['check','app_update_check'],['download','app_update_download'],['install','app_update_install']])$('app-update-'+id).addEventListener('click',()=>action(op));
 return {start(){void action('app_update_check');},stop(){stopped=true;clearTimer(timer);}};
}
