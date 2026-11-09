// SPDX-License-Identifier: MIT
import {WifiSetupClient,CloudSetupClient} from './shared/connection.mjs';
// Same authenticated editors on Android and desktop; durable saves stay explicit.
export function mountSettings({document,connection,lock,message,isBusy=()=>false,onChanged=async()=>{},wifiFactory=c=>new WifiSetupClient(c),cloudFactory=c=>new CloudSetupClient(c)}){
 const $=id=>document.getElementById(id);let wifi=null,cloud=null,rows=[],networks=[],pending=false,ownsLock=false,staged='',epoch=0,currentProfile=0;
 const keep=new Map();
 function notify(text){if($('settings-message'))$('settings-message').textContent=text;message(text);}
 function removed(){return rows.filter(row=>keep.get(row.id)?.checked===false);}
 function controls(){
  const other=isBusy()&&!ownsLock,blocked=pending||other||!!staged||!connection();
  for(const id of ['settings-ssid','settings-password','settings-open','settings-networks','settings-cloud-user','settings-cloud-password','settings-cloud-folder'])$(id).disabled=blocked;
  for(const id of ['settings-load','settings-scan','settings-check','settings-cloud-review'])$(id).disabled=blocked;
  for(const input of keep.values())input.disabled=blocked;
  if($('settings-keep-network'))$('settings-keep-network').disabled=blocked;
  if($('settings-remove-check'))$('settings-remove-check').disabled=blocked||!removed().length;
  $('settings-wifi-save').hidden=!['wifi','wifi-remove'].includes(staged);$('settings-wifi-save').textContent=staged==='wifi-remove'?'Save network removals':'Save Wi-Fi';
  $('settings-cloud-save').hidden=staged!=='cloud';
  for(const id of ['settings-wifi-save','settings-cloud-save'])$(id).disabled=pending||other;
  if($('settings-cancel')){$('settings-cancel').hidden=!staged;$('settings-cancel').disabled=pending;}
 }
 function keepOptions(){
  if(!$('settings-keep-network'))return;
  const select=$('settings-keep-network'),previous=Number(select.value);select.replaceChildren();
  const retained=rows.filter(row=>row.enabled&&keep.get(row.id)?.checked!==false);
  for(const row of retained){const option=document.createElement('option');option.value=String(row.id);option.textContent=row.ssid;select.append(option);}
  select.value=String(retained.some(r=>r.id===previous)?previous:retained.some(r=>r.id===currentProfile)?currentProfile:retained[0]?.id??'');
  if($('settings-removal'))$('settings-removal').hidden=!removed().length;controls();
 }
 function renderRows(){
  keep.clear();$('settings-saved').replaceChildren();
  for(const row of rows){
   const li=document.createElement('li'),label=document.createElement('label'),input=document.createElement('input'),name=document.createElement('span');
   input.type='checkbox';input.checked=true;input.setAttribute?.('aria-label','Keep '+row.ssid);keep.set(row.id,input);
   name.textContent=row.ssid+' · '+(row.id===currentProfile?'connected · ':'')+(row.enabled?'enabled':'disabled')+' · priority '+row.priority;
   label.append(input,name);li.append(label);$('settings-saved').append(li);
   input.addEventListener('change',()=>{
    if(!rows.some(r=>r.enabled&&keep.get(r.id)?.checked)){input.checked=true;notify('Keep at least one enabled Wi-Fi network. Add another working network before removing the last one.');}
    keepOptions();
   });
  }
  if($('settings-network-count'))$('settings-network-count').textContent=rows.length+' of 8 saved networks';keepOptions();
 }
 async function readRows(token){
  rows=await wifi.profiles();if(token!==epoch)return false;
  if(typeof wifi.status==='function'){const state=await wifi.status();if(token!==epoch)return false;currentProfile=state.wifi==='local_connected'?state.wifi_profile:0;}
  renderRows();return true;
 }
 function clear(){
  epoch++;wifi=cloud=null;rows=[];networks=[];keep.clear();pending=ownsLock=false;staged='';currentProfile=0;
  for(const id of ['settings-password','settings-cloud-password','settings-cloud-user','settings-cloud-folder','settings-ssid'])$(id).value='';
  $('settings-saved').replaceChildren();$('settings-networks').replaceChildren();if($('settings-keep-network'))$('settings-keep-network').replaceChildren();
  if($('settings-removal'))$('settings-removal').hidden=true;if($('settings-message'))$('settings-message').textContent='';$('stick-settings').hidden=true;controls();
 }
 async function action(kind){
  if(pending||!connection()||isBusy()&&!ownsLock||staged&&!['wifi-save','cloud-save','cancel'].includes(kind))return;
  const token=epoch,current=connection(),remove=removed().map(r=>r.id),retain=Number($('settings-keep-network')?.value);let durable=false;
  pending=ownsLock=true;lock(true);controls();
  try{
   if(['load','scan','check','remove'].includes(kind)){wifi??=wifiFactory(current);if(!await readRows(token))return;if(kind==='remove'){for(const id of remove)if(keep.has(id))keep.get(id).checked=false;keepOptions();$('settings-keep-network').value=String(retain);}}
   if(kind==='load')notify('Saved Wi-Fi networks read securely. Leave Keep checked for the networks you want.');
   if(kind==='scan'){
    networks=await wifi.scan({cancelled:()=>token!==epoch});if(token!==epoch)return;
    $('settings-networks').replaceChildren();const blank=document.createElement('option');blank.value='';blank.textContent='Choose a network';$('settings-networks').append(blank);
    for(const row of networks){if(!row.supported)continue;const option=document.createElement('option');option.value=String(networks.indexOf(row));option.textContent=row.ssid+' · '+row.rssi+' dBm'+(row.open?' · open network':'');$('settings-networks').append(option);}
    notify('Choose a nearby network, or enter a hidden network’s name.');
   }
   if(kind==='check'){
    const ssid=$('settings-ssid').value,password=$('settings-password').value,open=$('settings-open').checked,existing=rows.find(r=>r.ssid===ssid&&r.open===open);
    await wifi.stage({ssid,password,open,profile:existing?.id??0,keep:!password&&!!existing?.has_secret,priority:existing?.priority??0,enabled:true});$('settings-password').value='';
    notify('Checking this Wi-Fi connection. Keep this screen open…');await wifi.check({cancelled:()=>token!==epoch});if(token!==epoch)return;
    staged='wifi';notify('The stick connected to this Wi-Fi. Tap Save Wi-Fi to remember it.');
   }
   if(kind==='remove'){
    if(!remove.length||remove.length>7||remove.includes(retain)||!rows.some(r=>r.id===retain&&r.enabled)||remove.some(id=>!rows.some(r=>r.id===id)))throw Error('invalid_selection');
    await wifi.removeProfiles({profiles:remove,keep:retain});notify('Checking the Wi-Fi connection you chose to keep…');await wifi.check({cancelled:()=>token!==epoch});if(token!==epoch)return;
    staged='wifi-remove';notify('The retained Wi-Fi connection works. Save network removals to delete the unchecked networks from the stick.');
   }
   if(kind==='wifi-save'){
    await wifi.save();durable=true;if(token!==epoch)return;staged='';
    await readRows(token);if(token!==epoch)return;await onChanged('wifi');notify('Wi-Fi settings were saved and confirmed. Done closes Bluetooth so backups can resume.');
   }
   if(kind==='cloud-review'){
    cloud=cloudFactory(current);await cloud.status();await cloud.stage({username:$('settings-cloud-user').value,app_password:$('settings-cloud-password').value,folder:$('settings-cloud-folder').value});
    $('settings-cloud-password').value='';if(token!==epoch)return;staged='cloud';notify('Cloud changes are ready to save. This saves the destination; it does not test a cloud upload.');
   }
   if(kind==='cloud-save'){
    await cloud.save({acceptUnverified:true});durable=true;if(token!==epoch)return;staged='';$('settings-cloud-user').value='';$('settings-cloud-folder').value='';await onChanged('cloud');notify('Cloud settings were saved. Cloud access has not been tested. Done closes setup.');
   }
   if(kind==='cancel'){await wifi?.abort();await cloud?.abort();if(token!==epoch)return;staged='';for(const input of keep.values())input.checked=true;keepOptions();notify('Unsaved changes cancelled.');}
  }catch{
   if(token===epoch){
    if(durable){staged='';notify('Your save was confirmed, but this view could not refresh. Reconnect to read the saved settings.');}
    else if(kind==='wifi-save'||kind==='cloud-save'){notify('Save was not confirmed. Reconnect and read your settings before trying again.');current.disconnect();clear();}
    else{try{await wifi?.abort();await cloud?.abort();}catch{current.disconnect();}staged='';notify('This change could not be checked. Review the network or cloud details and reconnect if needed.');}
   }
  }finally{if(token===epoch){pending=false;ownsLock=!!staged;lock(ownsLock);controls();}}
 }
 for(const [id,kind]of [['load','load'],['scan','scan'],['check','check'],['wifi-save','wifi-save'],['cloud-review','cloud-review'],['cloud-save','cloud-save'],['remove-check','remove'],['cancel','cancel']])$('settings-'+id)?.addEventListener('click',()=>action(kind));
 $('settings-networks').addEventListener('change',()=>{const value=$('settings-networks').value,row=value===''?null:networks[Number(value)];if(row){$('settings-ssid').value=row.ssid;$('settings-open').checked=row.open;$('settings-password').value='';}});
 clear();return {clear,refreshLocks:controls,show(){$('stick-settings').hidden=false;controls();}};
}
