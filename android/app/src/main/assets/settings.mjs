// SPDX-License-Identifier: MIT
import {WifiSetupClient,CloudSetupClient} from './shared/connection.mjs';
// Reuse the authenticated clients and durable-save checks used by Setup.
export function mountSettings({document,connection,lock,message,wifiFactory=c=>new WifiSetupClient(c),cloudFactory=c=>new CloudSetupClient(c)}){
 const $=id=>document.getElementById(id);let wifi=null,cloud=null,rows=[],networks=[],pending=false,staged='',epoch=0;
 function controls(){for(const id of ['settings-ssid','settings-password','settings-open','settings-networks','settings-cloud-user','settings-cloud-password','settings-cloud-folder'])$(id).disabled=pending||!!staged;for(const id of ['settings-load','settings-scan','settings-check','settings-cloud-review'])$(id).disabled=pending||!!staged||!connection();$('settings-wifi-save').hidden=staged!=='wifi';$('settings-cloud-save').hidden=staged!=='cloud';$('settings-wifi-save').disabled=pending;$('settings-cloud-save').disabled=pending;}
 function clear(){epoch++;wifi=cloud=null;rows=[];networks=[];pending=false;staged='';for(const id of ['settings-password','settings-cloud-password','settings-cloud-user','settings-cloud-folder','settings-ssid'])$(id).value='';$('settings-saved').replaceChildren();$('settings-networks').replaceChildren();$('stick-settings').hidden=true;controls();}
 async function action(kind){
  if(pending||!connection()||staged&&!['wifi-save','cloud-save'].includes(kind))return;
  const token=epoch,current=connection();pending=true;lock(true);controls();
  try{
   if(kind==='load'||kind==='scan'||kind==='check'){
    wifi??=wifiFactory(current);rows=await wifi.profiles();if(token!==epoch)return;
    $('settings-saved').replaceChildren();for(const row of rows){const li=document.createElement('li');li.textContent=row.ssid+' · '+(row.enabled?'enabled':'disabled')+' · priority '+row.priority;$('settings-saved').append(li);}
   }
   if(kind==='load')message('Saved Wi-Fi networks read securely. Their passwords are never shown.');
   if(kind==='scan'){
    networks=await wifi.scan({cancelled:()=>token!==epoch});if(token!==epoch)return;
    $('settings-networks').replaceChildren();const blank=document.createElement('option');blank.value='';blank.textContent='Choose a network';$('settings-networks').append(blank);
    for(const row of networks){if(!row.supported)continue;const option=document.createElement('option');option.value=String(networks.indexOf(row));option.textContent=row.ssid+' · '+row.rssi+' dBm'+(row.open?' · open network':'');$('settings-networks').append(option);}
    message('Choose a nearby network, or enter a hidden network’s name.');
   }
   if(kind==='check'){
    const ssid=$('settings-ssid').value,password=$('settings-password').value,open=$('settings-open').checked;
    const existing=rows.find(r=>r.ssid===ssid&&r.open===open);
    await wifi.stage({ssid,password,open,profile:existing?.id??0,keep:!password&&!!existing?.has_secret,priority:existing?.priority??0,enabled:true});$('settings-password').value='';
    message('Checking this Wi-Fi connection. Keep this screen open…');
    await wifi.check({cancelled:()=>token!==epoch});if(token!==epoch)return;staged='wifi';message('The stick connected to this Wi-Fi. Tap Save Wi-Fi to remember it.');
   }
   if(kind==='wifi-save'){await wifi.save();if(token!==epoch)return;staged='';message('Wi-Fi settings were saved and confirmed. Done closes Bluetooth so backups can resume.');}
   if(kind==='cloud-review'){
    cloud=cloudFactory(current);await cloud.status();
    await cloud.stage({username:$('settings-cloud-user').value,app_password:$('settings-cloud-password').value,folder:$('settings-cloud-folder').value});
    $('settings-cloud-password').value='';if(token!==epoch)return;staged='cloud';message('Cloud changes are ready to save. This saves the destination; it does not test a cloud upload.');
   }
   if(kind==='cloud-save'){await cloud.save({acceptUnverified:true});if(token!==epoch)return;staged='';$('settings-cloud-user').value='';$('settings-cloud-folder').value='';message('Cloud settings were saved. Cloud access has not been tested. Done closes setup.');}
  }catch{
   if(token===epoch){
    if(kind==='wifi-save'||kind==='cloud-save'){message('Save was not confirmed. Reconnect and read your settings before trying again.');current.disconnect();clear();}
    else{try{await wifi?.abort();await cloud?.abort();}catch{current.disconnect();}staged='';message('This change could not be checked. Review the network or cloud details and reconnect if needed.');}
   }
  }finally{if(token===epoch){pending=false;lock(!!staged);controls();}}
 }
 for(const [id,kind]of [['load','load'],['scan','scan'],['check','check'],['wifi-save','wifi-save'],['cloud-review','cloud-review'],['cloud-save','cloud-save']])$('settings-'+id).addEventListener('click',()=>action(kind));
 $('settings-networks').addEventListener('change',()=>{const row=networks[Number($('settings-networks').value)];if(row){$('settings-ssid').value=row.ssid;$('settings-open').checked=row.open;$('settings-password').value='';}});
 clear();return {clear,show(){$('stick-settings').hidden=false;controls();}};
}
