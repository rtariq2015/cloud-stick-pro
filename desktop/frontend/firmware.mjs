// Installer state is native-owned. The renderer cannot supply images, paths,
// URLs, offsets, signature assertions or permission to flash.
const running=new Set(['checking','preparing','starting','connecting','backup_a','backup_b','checking_board','writing','verifying','verified','restarting']);
const messages={idle:'Start by checking the firmware available for your stick.',checking:'Checking the signed firmware release and its download…',available:'Verified firmware is ready. Choose a new stick or an existing Cloud Stick.',preparing:'Pausing backups and waiting for the stick to release its worker…',prepared:'Backups are paused. Safely eject the USB, unplug it, then hold BOOT as you plug it back in. Release BOOT and find the USB again.',starting:'Starting the protected installer…',connecting:'Checking the selected board in BOOT download mode…',backup_a:'Making the first encrypted recovery copy…',backup_b:'Making and checking the second encrypted recovery copy…',checking_board:'Checking the original firmware, ownership and partition layout…',writing:'Installing firmware. Keep the USB connected and the computer powered.',verifying:'Reading back the complete internal flash to check the firmware and preserved settings…',verified:'Firmware bytes verified. Checking the restart next…',restarting:'Waiting for the running firmware and checking its owner state…',installed:'Firmware installed and the restart checked. Continue to your login, Wi-Fi and cloud setup.',already_current:'This stick already has the offered firmware. Its restart has been checked.',failed:'The installer stopped before a firmware write. Follow the help below before trying again.',recovery_required:'Installation was interrupted. Recovery copies are kept privately on this computer. Resume is a separate action and requires the same stick.',boot_review:'Firmware bytes were verified, but normal startup could not be confirmed. Reconnect normally and use Diagnose my stick. Do not repeat the installation just to clear this message.'};
const help={firmware_app_update_required:'This firmware needs a newer Cloud Stick Setup app. Install the latest Windows or Linux app package, then check again.',firmware_preimage_unknown:'The installed firmware is not in the reviewed list. Installation is blocked; keep the recovery copies for review.',firmware_wrong_install_mode:'This is an existing Cloud Stick. Use Update my existing stick; a new-board installation cannot replace an owned stick.',firmware_existing_owner:'An existing Cloud Stick account was found. New-board installation is blocked.',firmware_existing_data:'Existing internal backup data was found. New-board installation is blocked.',private_memory_unavailable:'Linux needs a working systemd user session with memory controls. Windows needs its private-memory protection available. The app stopped before reading flash.',firmware_serial_busy:'Close serial monitors and other Cloud Stick apps, then try again.',firmware_storage_attached:'The stick is still exposing its USB drive. Safely eject it, unplug it, hold BOOT while reconnecting, then release BOOT.',firmware_pause_failed:'The stick could not confirm that backup work stopped safely. Reconnect normally and diagnose it before continuing.',firmware_device_changed:'The selected USB changed. Select the same original stick again.',firmware_protected_state_changed:'Settings or other protected bytes changed after the interrupted write. Automatic recovery is blocked; keep the private recovery copies for review.',firmware_board_unsupported:'Only the original 16 MB LILYGO T-Dongle S3 without PSRAM is supported.',firmware_security_unsupported:'This board uses an unsupported security configuration. No security setting was changed.',firmware_package_unavailable:'The bundled firmware package is missing or damaged. Install the complete reviewed app package.',firmware_worker_start_failed:'The protected installer could not start. On Linux, use a desktop session with systemd user services. On Windows, check that the complete app folder is installed.',firmware_boot_check_failed:'Firmware bytes passed read-back, but startup needs review. Reconnect normally and use diagnostics.'};
export function firmwareState(value){
 if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).some(k=>!['phase','done','total','code','online','version','can_resume'].includes(k))||!Object.hasOwn(messages,value.phase)||!Number.isSafeInteger(value.done)||!Number.isSafeInteger(value.total)||value.done<0||value.total<1||value.done>value.total||value.total>67108864||typeof value.code!=='string'||!/^[a-z_]{0,64}$/.test(value.code)||typeof value.online!=='boolean'||typeof value.version!=='string'||!/^\d{1,3}\.\d{1,3}\.\d{1,3}(?:-[a-z0-9.-]+)?$/.test(value.version)||(value.can_resume!==undefined&&typeof value.can_resume!=='boolean'))throw Error('invalid_firmware_state');
 return {...value};
}
export function firmwareMessage(state){return messages[state.phase]+(state.phase==='available'?` Firmware: ${state.version}. ${state.online?'Online release checked.':'Online check unavailable; using the verified copy on this computer.'}`:'');}
export function mountFirmwarePanel({document,request,canUse,setBusy,onContinue,setTimer=setTimeout,clearTimer=clearTimeout}){
 const $=id=>document.getElementById(id);let state={phase:'idle',done:0,total:1,code:'',online:false,version:'0.0.0'},pending=false,timer=null,stopped=false,actionHelp='',installed='';
 function render(){
  const active=running.has(state.phase),blocked=active||state.can_resume===true;
  setBusy(blocked);const locked=pending||!canUse()||active;
  $('firmware-check').disabled=locked||state.can_resume===true;if($('firmware-import'))$('firmware-import').disabled=locked||state.can_resume===true;
  $('firmware-ports-refresh').disabled=locked;
  $('firmware-mode').disabled=locked||state.can_resume===true;
  $('firmware-ports').disabled=locked;
  $('firmware-prepare').hidden=$('firmware-mode').value!=='existing';
  $('firmware-prepare').disabled=(installed===state.version)||locked||!$('firmware-ports').value||state.phase!=='available';
  $('firmware-install').disabled=($('firmware-mode').value==='existing'&&installed===state.version)||locked||!$('firmware-ports').value||state.phase!==($('firmware-mode').value==='existing'?'prepared':'available')||state.can_resume===true;
  $('firmware-resume').hidden=state.can_resume!==true;$('firmware-resume').disabled=locked||!$('firmware-ports').value;
  $('firmware-continue').hidden=!['installed','already_current','boot_review'].includes(state.phase);
  $('firmware-message').textContent=state.phase==='available'&&installed===state.version?'Your stick already has the latest offered firmware: '+state.version+'.':firmwareMessage(state);
  if($('firmware-latest-note'))$('firmware-latest-note').textContent=state.phase==='available'?(installed===state.version?'Up to date':installed?'Update available':'Firmware checked'):state.phase==='checking'?'Checking…':'';
  $('firmware-help').textContent=actionHelp||(help[state.code]??(state.phase==='failed'||state.phase==='recovery_required'?'Keep the USB connected. Review its BOOT mode, app permissions and power before continuing. No automatic write retry is made.':''));
  $('firmware-progress').hidden=!active;$('firmware-progress').max=state.total;$('firmware-progress').value=state.done;
 }
 function schedule(){if(timer===null&&!stopped)timer=setTimer(()=>{timer=null;void poll();},2000);}
 async function poll(){
  if(stopped)return;
  try{state=firmwareState(await request('firmware_state'));render();}catch{$('firmware-help').textContent='Installer status is unavailable. Keep the stick connected and reopen the app to check the existing job.';}
  if(state.phase==='prepared'&&!pending&&canUse())await action('firmware_ports');
  if(running.has(state.phase)||state.phase==='recovery_required'||state.phase==='prepared')schedule();
 }
 async function action(op,args={}){
  if(pending||!canUse()||stopped)return;
  pending=true;actionHelp='';render();
  try{
   const result=await request(op,args);
   if(op==='firmware_ports'){
    if(!Array.isArray(result)||result.length>64||result.some(x=>!x||typeof x.port!=='string'||!/^COM[1-9][0-9]{0,3}$|^\/dev\/tty(?:ACM|USB)[0-9]{1,4}$/.test(x.port)||typeof x.label!=='string'||x.label.length>256))throw Error('invalid_ports');
    const select=$('firmware-ports'),previous=select.value;select.replaceChildren();const blank=document.createElement('option');blank.value='';blank.textContent='Choose your stick';select.append(blank);
    for(const row of result){const option=document.createElement('option');option.value=row.port;option.textContent=row.label;select.append(option);}
    if(result.some(row=>row.port===previous))select.value=previous;else if(result.length===1)select.value=result[0].port;
   }else if(!result?.cancelled)state=firmwareState(result);
  }catch(e){actionHelp=help[e?.code]??'The operation could not start. Close setup and other USB tools, then check the selected stick again.';}
  finally{pending=false;render();if(running.has(state.phase)||state.phase==='prepared')schedule();}
 }
 $('firmware-import')?.addEventListener('click',()=>action('firmware_import'));
 $('firmware-check').addEventListener('click',()=>action('firmware_check'));
 $('firmware-ports-refresh').addEventListener('click',()=>action('firmware_ports'));
 $('firmware-prepare').addEventListener('click',()=>action('firmware_prepare',{port:$('firmware-ports').value}));
 $('firmware-install').addEventListener('click',()=>action('firmware_start',{port:$('firmware-ports').value,mode:$('firmware-mode').value}));
 $('firmware-resume').addEventListener('click',()=>action('firmware_start',{port:$('firmware-ports').value,mode:'resume'}));
 $('firmware-mode').addEventListener('change',render);$('firmware-ports').addEventListener('change',render);
 $('firmware-continue').addEventListener('click',()=>{if(pending||running.has(state.phase))return;$('firmware-panel').open=false;onContinue();});
 render();return {setInstalled(value){installed=typeof value==='string'?value:'';render();},autoCheck(value){installed=value;return action('firmware_check');},refreshLocks:render,restore:poll,open(mode){$('firmware-panel').open=true;if(mode)$('firmware-mode').value=mode;render();$('firmware-check').focus();void (async()=>{await action('firmware_ports');if(!state.can_resume)await action('firmware_check');})();},stop(){stopped=true;if(timer!==null)clearTimer(timer);}};
}
