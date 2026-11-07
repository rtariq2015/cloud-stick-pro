import {usbFailureHelp} from './onboarding.mjs';

const invalid=()=>Object.assign(new Error('The safe health snapshot was not recognized.'),{code:'invalid_diagnostics'});
const exact=(value,keys)=>value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===keys.length&&Object.keys(value).every(k=>keys.includes(k));
const uint=(value,max=0xffffffff)=>Number.isSafeInteger(value)&&value>=0&&value<=max;
const sint=value=>Number.isSafeInteger(value)&&value>=-0x80000000&&value<=0x7fffffff;
const decimal=value=>typeof value==='string'&&/^[0-9]{1,20}$/.test(value)&&BigInt(value)<(1n<<64n);
const version=value=>typeof value==='string'&&value.length<=36&&/^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}(?:-[a-z0-9-]+(?:\.[a-z0-9-]+)*)?$/.test(value);
const statusBools=['setup_only','owner_ready','store_fault','ble','internet_verified','backups_paused'];
const lifecycleBools=['ble_recovery_checked','ble_memory_restored'];
const lifecycleNumbers=['ble_before_free','ble_before_largest','ble_after_free','ble_after_largest','save_before_free','save_before_largest','save_after_free','save_after_largest','internal_minimum','loop_stack_free','host_exit_stack_min'];
const backupBools=['cloud_configured','paused','active','has_scan','scan_complete'];
const backupNumbers=['worker_generation','verified','pending','failed','speed','worker_stack_free','tls_minimum_heap'];
const wifiCodes=['idle','scanning','connecting','local_connected','failed','fault','cancelling'];
function fields(value,bools,numbers,extra=[]){
 if(!exact(value,[...bools,...numbers,...extra])||bools.some(k=>typeof value[k]!=='boolean')||numbers.some(k=>!uint(value[k])))throw invalid();
}
// Strictly decode the native allowlist before storing or displaying any part.
// Unknown fields (including paths, names or credentials) reject the whole reply.
export function validateDiagnostics(value){
 if(!exact(value,['schema','snapshot_kind','status','radio','lifecycle','boot','memory','backup','dashboard'])||value.schema!=='cloud-stick-diagnostics/v1'||value.snapshot_kind!=='sequential_read_only')throw invalid();
 const {status,radio,lifecycle,boot,memory,backup,dashboard}=value;
 fields(status,statusBools,['internal_free','largest_block'],['firmware','wifi','saved_revision']);
 if(!version(status.firmware)||!wifiCodes.includes(status.wifi)||!decimal(status.saved_revision)||status.internet_verified!==false)throw invalid();
 if(!exact(radio,['wifi_step','wifi_sdk_error','wifi_error'])||!uint(radio.wifi_step,28)||!sint(radio.wifi_sdk_error)||!uint(radio.wifi_error,20))throw invalid();
 fields(lifecycle,lifecycleBools,lifecycleNumbers);
 if(!exact(boot,['uptime_ms','reset_reason'])||!uint(boot.uptime_ms)||!uint(boot.reset_reason,255))throw invalid();
 fields(memory,['failure_hook_registered','allocation_failure_seen'],['samples','observed_min_free','observed_min_largest']);
 fields(backup,backupBools,backupNumbers,['worker_phase','worker_error','last_backup_unix','error','phase','done','total']);
 if(!uint(backup.worker_phase,5)||!sint(backup.worker_error)||!uint(backup.last_backup_unix,253402300799)||!sint(backup.error)||!uint(backup.phase,4)||!decimal(backup.done)||!decimal(backup.total))throw invalid();
 if(!exact(dashboard,['dashboard_listening','dashboard_fault','station_ip'])||typeof dashboard.dashboard_listening!=='boolean'||typeof dashboard.dashboard_fault!=='boolean'||typeof dashboard.station_ip!=='string'||!/^\d{1,3}(?:\.\d{1,3}){3}$/.test(dashboard.station_ip)||dashboard.station_ip.split('.').some(s=>String(Number(s))!==s||!uint(Number(s),255)))throw invalid();
 // Clone only after all branches pass, so callers cannot mutate a stored view.
 return JSON.parse(JSON.stringify(value));
}
export function safeDiagnosticExport(value){
 const data=validateDiagnostics(value);
 // The local network address is for on-screen help only, never generic export.
 delete data.dashboard.station_ip;
 return data;
}
export function serviceAllowed(state,{selected=false,usbBusy=false,transportBusy=false,firstOwnerPending=false}={}){
 return selected===true&&!usbBusy&&!transportBusy&&!firstOwnerPending&&state?.authenticated!==true&&state?.busy!==true&&state?.loginStaged!==true&&state?.cloudStaged!==true&&state?.portalPrepared!==true&&state?.wifiCandidatePending!==true;
}
const memorySize=value=>(value/1024).toLocaleString(undefined,{maximumFractionDigits:1})+' KiB';
const uptime=value=>{const seconds=Math.floor(value/1000),days=Math.floor(seconds/86400),hours=Math.floor(seconds%86400/3600),minutes=Math.floor(seconds%3600/60);return (days?days+'d ':'')+hours+'h '+minutes+'m';};
const recordedTime=value=>value>=1767225600?new Date(value*1000).toLocaleString():'No usable recorded backup time';
// Interpret reported state only. A sequential read cannot establish the cause
// of a network/cloud failure or authorize a reset, repair or settings change.
export function diagnosticAdvice(value){
 const d=validateDiagnostics(value),s=d.status,b=d.backup,l=d.lifecycle;
 const advice=(code,title,detail,next)=>({code,title,detail,next});
 if(s.store_fault)return advice('settings_review','Settings storage needs review','The stick reported a settings-storage fault.','Stop setup changes and copy the safe diagnostic report for review.');
 if(!s.owner_ready)return advice('owner_review','Owner access needs review','The stick did not report a ready owner login.','Check the existing owner or recovery status before changing Wi-Fi.');
 if(d.memory.allocation_failure_seen)return advice('memory_review','Memory needs review','The stick recorded a memory allocation failure during this boot.','Keep it connected and copy the safe diagnostic report for review.');
 if(b.worker_error||b.worker_phase===5)return advice('worker_review','Backup service needs review','The backup worker reported a problem.','Copy the safe diagnostic report before making more setup changes.');
 if(s.ble)return advice('setup_open','Bluetooth setup is still open','Backups can remain paused while setup is open.','Finish or disconnect in the setup app, wait up to 30 seconds, then diagnose again.');
 if((s.backups_paused||b.paused)&&l.ble_recovery_checked&&!l.ble_memory_restored)return advice('bluetooth_cleanup_review','Bluetooth cleanup needs review','The last Bluetooth close did not confirm that its memory was recovered. Backups are paused.','Wait 30 seconds and diagnose again. If this remains, copy the safe report and leave the stick connected for review.');
 if(s.backups_paused||b.paused)return advice('backup_paused','Backups are paused','The snapshot reports a paused backup service. It does not establish why.','If setup just closed, wait 30 seconds and diagnose again. Copy the safe report if it stays paused.');
 if(s.wifi!=='local_connected')return advice('wifi_waiting','Wi-Fi is not connected','The stick has not reported a local Wi-Fi connection.','Keep a previously saved network or hotspot on, wait for reconnection, then diagnose again.');
 if(!b.cloud_configured)return advice('cloud_setup_needed','Cloud setup is incomplete','No usable cloud configuration was reported.','Sign in to setup and review the saved cloud-account settings.');
 if(b.error||b.failed)return advice('backup_retry','Backup results need review','The last backup result includes an error or files needing another attempt.','Review the reported backup code below. This snapshot cannot check the account or identify the cause.');
 if(!b.has_scan||!b.scan_complete||b.worker_phase!==2)return advice('backup_waiting','Waiting for the backup service','The stick has not reported a running worker with a complete recorded scan.','Leave it connected and allow the scan to finish, then diagnose again.');
 if(d.dashboard.dashboard_fault)return advice('dashboard_review','Local dashboard needs review','The dashboard reported a problem; this alone does not establish a backup failure.','Copy the safe diagnostic report if the local dashboard remains unavailable.');
 return advice('local_ready','Local connection and backup service are available','The stick reports Wi-Fi connected and its backup worker running. Recorded file results appear below.','No setup change is indicated by this snapshot. Cloud access and file integrity were not tested.');
}
export function diagnosticFacts(value){
 const d=validateDiagnostics(value),b=d.backup,advice=diagnosticAdvice(d);
 return [
  ['What the stick reports',advice.title+'. '+advice.detail],['What to do next',advice.next],
  ['Firmware',d.status.firmware],['USB management','Responded. SD card and file integrity were not tested.'],
  ['Owner login',d.status.owner_ready?'Ready on the stick':'Not ready — check the owner/recovery status during setup.'],
  ['Wi-Fi',d.status.wifi==='local_connected'?'Connected to a local network. This refresh does not test internet or Koofr.':({'idle':'Not connected','scanning':'Searching','connecting':'Connecting','failed':'Connection failed','fault':'Needs review','cancelling':'Closing a network attempt'}[d.status.wifi])],
  ['Settings storage',d.status.store_fault?'The settings store reports a fault. Stop changes and review recovery.':'No settings-store fault reported. This is not an SD health test.'],
  ['Cloud configuration',b.cloud_configured?'Present; account access was not tested by this refresh.':'No usable cloud configuration reported.'],
  ['Backup service',b.paused?'Paused':(['Stopped','Starting','Running','Finishing its current work','Drained; cleanup pending','Needs review'][b.worker_phase])],
  ['Recorded file results',b.has_scan?b.verified+' verified / '+b.pending+' pending / '+b.failed+' needing retry':'No recorded scan snapshot'],
  ['Scan completeness',b.has_scan?(b.scan_complete?'Last recorded scan was complete.':'Last recorded scan was partial. Counts may be incomplete.'):'Not recorded'],
  ['Last recorded backup',recordedTime(b.last_backup_unix)],
  ['Last reported error',b.error||b.worker_error?'Backup needs review. Device backup code '+b.error+' / worker code '+b.worker_error+'.':'No backup or worker error reported. This is not a fresh cloud check.'],
  ['Free internal memory',memorySize(d.status.internal_free)+'; largest block '+memorySize(d.status.largest_block)],
  ['Lowest observed memory',d.memory.samples?memorySize(d.memory.observed_min_free)+' free; largest block minimum '+memorySize(d.memory.observed_min_largest):'No samples recorded'],
  ['Allocation failures',d.memory.allocation_failure_seen?'A failure was recorded. Review the device before further changes.':'No allocation failure recorded by this diagnostic'],
  ['Uptime',uptime(d.boot.uptime_ms)+'; reset reason code '+d.boot.reset_reason],
  ['Local dashboard',d.dashboard.dashboard_fault?'Dashboard reports a fault':d.dashboard.dashboard_listening&&d.dashboard.station_ip!=='0.0.0.0'?'http://'+d.dashboard.station_ip+'/ — available only from a reachable local network.':'No local dashboard address is available in this snapshot']
 ];
}
export function validateBundleReview(value){
 if(exact(value,['cancelled'])&&value.cancelled===true)return {cancelled:true};
 if(!exact(value,['version','board','sha256','bytes','signature_status','can_install','message'])||!version(value.version)||typeof value.board!=='string'||!/^[A-Za-z0-9._-]{1,96}$/.test(value.board)||typeof value.sha256!=='string'||!/^[0-9a-f]{64}$/.test(value.sha256)||!uint(value.bytes,16*1024*1024)||value.bytes<1||value.signature_status!=='Not verified'||value.can_install!==false||typeof value.message!=='string'||value.message.length>512)throw invalid();
 return {version:value.version,board:value.board,sha256:value.sha256,bytes:value.bytes};
}
export function mountServicePanel({document,canUse,readDiagnostics,reviewBundle,openRecords,clipboard=globalThis.navigator?.clipboard}){
 const $=id=>document.getElementById(id);let busy=false,epoch=0,snapshot=null,stopped=false;
 function controls(){const allowed=!stopped&&!busy&&canUse();for(const id of ['service-refresh','service-review','service-records'])$(id).disabled=!allowed;$('service-copy').disabled=!allowed||!snapshot;}
 function clear(){++epoch;snapshot=null;$('service-facts').replaceChildren();$('service-summary').replaceChildren();$('service-read-at').textContent='';$('service-attention').hidden=true;$('service-attention').textContent='';$('service-json').value='';$('service-health').hidden=true;$('service-bundle').textContent='';$('service-bundle').hidden=true;$('service-message').textContent='No snapshot loaded. Click Diagnose my stick when you are ready.';controls();}
 async function refresh(){
  if(busy||stopped||!canUse())return;busy=true;controls();const current=epoch;$('service-message').textContent='Reading one local USB snapshot…';
  snapshot=null;$('service-json').value='';$('service-facts').replaceChildren();$('service-summary').replaceChildren();$('service-read-at').textContent='';$('service-attention').hidden=true;$('service-attention').textContent='';$('service-health').hidden=true;
  try{const value=validateDiagnostics(await readDiagnostics());if(current!==epoch||stopped||!canUse())return;
   snapshot=value;const facts=diagnosticFacts(value);for(const [label,text]of facts){const name=document.createElement('dt'),detail=document.createElement('dd');name.textContent=label;detail.textContent=text;$('service-facts').append(name,detail);}
   const wifi={'local_connected':'Local connection',idle:'Not connected',scanning:'Searching',connecting:'Connecting',cancelling:'Finishing an attempt',failed:'Connection failed',fault:'Needs review'}[value.status.wifi];
   for(const [label,text,note]of [['Wi-Fi now',wifi,'Internet access not tested'],['Worker now',Object.fromEntries(facts)['Backup service'],'A worker state, not file proof'],['Recorded verified',value.backup.has_scan?String(value.backup.verified):'Not recorded',value.backup.has_scan?(value.backup.scan_complete?'Previous scan complete':'Previous scan partial'):'No saved scan snapshot']]){
    const card=document.createElement('div'),name=document.createElement('span'),result=document.createElement('strong'),detail=document.createElement('small');card.className='summary-card';name.textContent=label;result.textContent=text;detail.textContent=note;card.append(name,result,detail);$('service-summary').append(card);
   }
   $('service-read-at').textContent='Read at '+new Date().toLocaleTimeString()+'. Local USB values were read in sequence; file counts and backup dates are recorded results.';
   const advice=diagnosticAdvice(value);
   const attention=advice.title+'. '+advice.detail+' '+advice.next;
   $('service-attention').hidden=false;$('service-attention').textContent=attention;
   $('service-json').value=JSON.stringify(safeDiagnosticExport(value),null,2);$('service-health').hidden=false;$('service-message').textContent='Snapshot read. These fields were read in sequence and may reflect different moments. This did not run an upload or contact Koofr.';
  }catch(error){if(current===epoch&&!stopped)$('service-message').textContent=error?.code==='invalid_diagnostics'?'This firmware’s health reply was not recognized. Nothing was exported or changed.':usbFailureHelp(error);}
  finally{busy=false;controls();}
 }
 async function review(){
  if(busy||stopped||!canUse())return;busy=true;controls();const current=epoch;$('service-bundle').textContent='';$('service-bundle').hidden=true;
  try{const result=validateBundleReview(await reviewBundle());if(current!==epoch||stopped||!canUse())return;
   $('service-bundle').textContent=result.cancelled?'Bundle review cancelled.':`Bundle version: ${result.version}\nBoard: ${result.board}\nApp bytes: ${result.bytes}\nSHA-256: ${result.sha256}\nSignature: not verified. Device compatibility: not established. Recovery backup: not verified here.\nInstallation is unavailable: this screen has no integrated firmware writer.`;$('service-bundle').hidden=false;
  }catch{if(current===epoch&&!stopped){$('service-bundle').textContent='The bundle review could not finish. No firmware was installed.';$('service-bundle').hidden=false;}}
  finally{busy=false;controls();}
 }
 $('service-refresh').addEventListener('click',refresh);$('service-review').addEventListener('click',review);
 $('service-copy').addEventListener('click',async()=>{
  if(busy||stopped||!canUse()||!snapshot)return;const current=epoch,text=JSON.stringify(safeDiagnosticExport(snapshot),null,2);
  try{if(typeof clipboard?.writeText!=='function')throw Error('Clipboard unavailable');await clipboard.writeText(text);if(current===epoch&&!stopped)$('service-message').textContent='Safe diagnostic JSON copied. No Wi-Fi names, account details, paths or file history are included.';}
  catch{if(current===epoch&&!stopped){$('service-json-details').open=true;$('service-json').focus();$('service-json').select();$('service-message').textContent='Select and copy the safe JSON below. Automatic clipboard access was unavailable.';}}
 });
 $('service-records').addEventListener('click',()=>{if(!busy&&!stopped&&canUse())openRecords();});
 clear();return {refreshLocks:controls,clear,stop(){stopped=true;clear();},get busy(){return busy;}};
}
