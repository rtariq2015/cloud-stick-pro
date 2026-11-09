// SPDX-License-Identifier: MIT
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
 async function action(op){if(pending||!idle()||stopped)return;pending=true;try{const s=await request(op);if(!s.cancelled&&!s.installing)render(s);}catch{last=null;$('app-update-message').textContent=op==='app_update_install'?'Finish Bluetooth setup first. If Android opens its install permission screen, allow Cloud Stick updates, return here and tap Install again.':'The update could not start. Check your connection and try again.';}finally{pending=false;if(last)render(last);clearTimer(timer);if(['checking','downloading'].includes(phase))timer=setTimer(poll,2500);}}
 document.addEventListener('visibilitychange',()=>{clearTimer(timer);if(!document.hidden&&['checking','downloading'].includes(phase))void poll();});
 for(const [id,op]of [['check','app_update_check'],['download','app_update_download'],['install','app_update_install']])$('app-update-'+id).addEventListener('click',()=>action(op));
 return {start(){void action('app_update_check');},stop(){stopped=true;clearTimer(timer);}};
}
