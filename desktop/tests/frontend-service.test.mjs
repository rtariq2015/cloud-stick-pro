import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {validateDiagnostics,safeDiagnosticExport,diagnosticFacts,diagnosticAdvice,serviceAllowed,validateBundleReview,mountServicePanel} from '../frontend/service.mjs';
import {SetupWizardModel} from '../frontend/wizard-model.mjs';
import {DesktopBridge} from '../frontend/bridge.mjs';
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
function snapshot(){return {
 schema:'cloud-stick-diagnostics/v1',snapshot_kind:'sequential_read_only',
 status:{setup_only:false,owner_ready:true,store_fault:false,ble:false,internet_verified:false,backups_paused:false,firmware:'0.4.1-setup-dev',wifi:'local_connected',saved_revision:'18446744073709551615',internal_free:64256,largest_block:32768},
 radio:{wifi_step:0,wifi_sdk_error:0,wifi_error:0},
 lifecycle:{ble_recovery_checked:true,ble_memory_restored:true,...Object.fromEntries(['ble_before_free','ble_before_largest','ble_after_free','ble_after_largest','save_before_free','save_before_largest','save_after_free','save_after_largest','internal_minimum','loop_stack_free','host_exit_stack_min'].map(k=>[k,20480]))},
 boot:{uptime_ms:3700000,reset_reason:1},memory:{failure_hook_registered:true,samples:8,observed_min_free:32768,observed_min_largest:16384,allocation_failure_seen:false},
 backup:{worker_phase:2,worker_generation:1,worker_error:0,cloud_configured:true,paused:false,active:false,has_scan:true,scan_complete:true,verified:10,pending:0,failed:0,last_backup_unix:1793867547,error:0,phase:0,done:'18446744073709551615',total:'18446744073709551615',speed:0,worker_stack_free:4096,tls_minimum_heap:32768},
 dashboard:{dashboard_listening:true,dashboard_fault:false,station_ip:'192.0.2.9'}
};}
test('strict native schema keeps exact uint64 strings and excludes unknown fields at every level',()=>{
 const data=snapshot(),valid=validateDiagnostics(data);assert.deepEqual(valid,data);assert.notEqual(valid,data);assert.equal(valid.backup.done,'18446744073709551615');
 for(const branch of ['', 'status','radio','lifecycle','boot','memory','backup','dashboard']){const value=snapshot();(branch?value[branch]:value).password='SYNTHETIC-SECRET';assert.throws(()=>validateDiagnostics(value));}
 for(const edit of [x=>x.backup.done=18446744073709551615,x=>x.backup.total='18446744073709551616',x=>x.status.saved_revision='18446744073709551616',x=>x.status.owner_ready='true',x=>x.status.internet_verified=true,x=>x.radio.wifi_step=29,x=>x.radio.wifi_sdk_error=2147483648,x=>x.backup.worker_phase=6,x=>x.backup.phase=5,x=>x.boot.reset_reason=-1,x=>x.memory.samples=false,x=>x.dashboard.station_ip='192.000.2.9',x=>x.dashboard.station_ip='192.0.2.9/private']){const value=snapshot();edit(value);assert.throws(()=>validateDiagnostics(value));}
});
test('safe export contains only explicit diagnostic values and omits the local address',()=>{
 const data=snapshot(),exported=safeDiagnosticExport(data),text=JSON.stringify(exported);assert.equal(Object.hasOwn(exported.dashboard,'station_ip'),false);assert.doesNotMatch(text,/192\.0\.2\.9|ssid|username|password|owner_key|file_history|path|device_id/i);assert.equal(data.dashboard.station_ip,'192.0.2.9');
 const visit=value=>{if(value&&typeof value==='object')for(const child of Object.values(value))visit(child);else assert.ok(typeof value==='boolean'||typeof value==='number'||typeof value==='string'&&/^[a-z0-9_./-]+$/.test(value));};visit(exported);
});
test('plain facts distinguish serial/settings/recorded results from SD health and a fresh cloud test',()=>{
 const facts=Object.fromEntries(diagnosticFacts(snapshot()));assert.match(facts['USB management'],/SD card and file integrity were not tested/);assert.match(facts['Wi-Fi'],/does not test internet or Koofr/);assert.match(facts['Cloud configuration'],/was not tested/);assert.equal(facts['Recorded file results'],'10 verified / 0 pending / 0 needing retry');assert.match(facts['Local dashboard'],/192\.0\.2\.9/);assert.equal(facts['Backup service'],'Running');
 assert.match(facts['Last reported error'],/No backup or worker error reported/);const empty=snapshot();empty.backup.has_scan=false;empty.backup.last_backup_unix=0;empty.memory.samples=0;empty.backup.error=-2402;assert.equal(Object.fromEntries(diagnosticFacts(empty))['Recorded file results'],'No recorded scan snapshot');assert.equal(Object.fromEntries(diagnosticFacts(empty))['Last recorded backup'],'No usable recorded backup time');assert.match(Object.fromEntries(diagnosticFacts(empty))['Last reported error'],/needs review.*-2402/);
});
test('service admission blocks live authentication, staged data and pending native ownership without closing it',()=>{
 assert.equal(serviceAllowed({}, {selected:true}),true);assert.equal(serviceAllowed({}, {selected:false}),false);
 for(const flag of ['authenticated','busy','loginStaged','cloudStaged','portalPrepared','wifiCandidatePending'])assert.equal(serviceAllowed({[flag]:true},{selected:true}),false,flag);
 for(const flag of ['usbBusy','transportBusy','firstOwnerPending'])assert.equal(serviceAllowed({},{selected:true,[flag]:true}),false,flag);
 const m=new SetupWizardModel();assert.equal(m.openService(true),false);m.next();m.checkedUsb({ownerReady:true});assert.equal(m.openService(true),true);assert.equal(m.step,'service');assert.equal(m.closeService(),true);assert.equal(m.step,'usb');
 m.state.authenticated=true;assert.equal(m.openService(true),false);assert.equal(m.state.authenticated,true,'Opening service must never disconnect a session');
});
test('local bundle review cannot claim signature, compatibility, recovery or a writer approval',()=>{
 const good={version:'0.4.1-setup-dev',board:'lilygo-t-dongle-s3',sha256:'a'.repeat(64),bytes:1681344,signature_status:'Not verified',can_install:false,message:'Manifest inspection only.'};assert.equal(validateBundleReview(good).version,good.version);assert.deepEqual(validateBundleReview({cancelled:true}),{cancelled:true});
 for(const edit of [x=>x.can_install=true,x=>x.signature_status='Verified',x=>x.path='/private/file',x=>x.sha256='bad',x=>x.bytes=-1]){const value={...good};edit(value);assert.throws(()=>validateBundleReview(value));}
});
function ui({read,review,clipboard}={}){
 class Element{constructor(){this.disabled=false;this.hidden=true;this.value='';this.textContent='';this.children=[];this.listeners={};this.focused=false;this.selected=false;}append(...x){this.children.push(...x);}replaceChildren(...x){this.children=x;}addEventListener(k,f){this.listeners[k]=f;}focus(){this.focused=true;}select(){this.selected=true;}click(){if(!this.disabled)return this.listeners.click?.();}}
 const elements=new Map(),get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};let allowed=true,reads=0,reviews=0,records=0;
 const panel=mountServicePanel({document:{getElementById:get,createElement:()=>new Element()},canUse:()=>allowed,readDiagnostics:()=>{++reads;return read?read():Promise.resolve(snapshot());},reviewBundle:()=>{++reviews;return review?review():Promise.resolve({cancelled:true});},openRecords:()=>++records,clipboard});
 return {panel,get,setAllowed(value){allowed=value;panel.refreshLocks();},get reads(){return reads;},get reviews(){return reviews;},get records(){return records;}};
}
test('service never polls; each explicit refresh produces one validated snapshot',async()=>{
 const f=ui();assert.equal(f.reads,0);assert.equal(f.get('service-copy').disabled,true);await f.get('service-refresh').click();assert.equal(f.reads,1);assert.equal(f.get('service-health').hidden,false);assert.equal(f.get('service-copy').disabled,false);assert.match(f.get('service-message').textContent,/did not run an upload or contact Koofr/);assert.doesNotMatch(f.get('service-json').value,/192\.0\.2\.9/);await tick();assert.equal(f.reads,1);await f.get('service-refresh').click();assert.equal(f.reads,2);
});
test('service summary separates current local connection from recorded file results and clears on selection change',async()=>{
 const f=ui();await f.get('service-refresh').click();assert.equal(f.get('service-summary').children.length,3);const text=f.get('service-summary').children.map(card=>card.children.map(n=>n.textContent).join(' ')).join(' ');assert.match(text,/Wi-Fi now Local connection/);assert.match(text,/Recorded verified 10 Previous scan complete/);assert.match(f.get('service-read-at').textContent,/read in sequence.*recorded results/);f.panel.clear();assert.equal(f.get('service-summary').children.length,0);assert.equal(f.get('service-read-at').textContent,'');
});
test('copy uses only safe projected JSON and has a manual-copy fallback',async()=>{
 const writes=[],f=ui({clipboard:{async writeText(text){writes.push(text);}}});await f.get('service-refresh').click();await f.get('service-copy').click();assert.equal(writes.length,1);assert.doesNotMatch(writes[0],/192\.0\.2\.9|password|file_history|ssid/);
 const fallback=ui({clipboard:{async writeText(){throw Error('SYNTHETIC private OS text');}}});await fallback.get('service-refresh').click();await fallback.get('service-copy').click();assert.equal(fallback.get('service-json-details').open,true);assert.equal(fallback.get('service-json').selected,true);assert.doesNotMatch(fallback.get('service-message').textContent,/SYNTHETIC|private OS/);
});
test('malformed diagnostics reveal no partial facts or export and do not retry',async()=>{
 const invalid=snapshot();invalid.status.ssid='SYNTHETIC-PRIVATE';const f=ui({read:async()=>invalid});await f.get('service-refresh').click();assert.equal(f.reads,1);assert.equal(f.get('service-health').hidden,true);assert.equal(f.get('service-json').value,'');assert.equal(f.get('service-copy').disabled,true);assert.doesNotMatch(f.get('service-message').textContent,/SYNTHETIC|ssid/);assert.match(f.get('service-message').textContent,/Nothing was exported or changed/);
});
test('selection/lifecycle clear invalidates late responses and stale clipboard output',async()=>{
 const gate=deferred(),f=ui({read:()=>gate.promise}),pending=f.get('service-refresh').click();f.panel.clear();gate.resolve(snapshot());await pending;assert.equal(f.get('service-health').hidden,true);assert.equal(f.get('service-json').value,'');assert.equal(f.get('service-copy').disabled,true);
 await f.get('service-refresh').click();assert.equal(f.get('service-health').hidden,false);f.panel.stop();assert.equal(f.get('service-json').value,'');assert.equal(f.get('service-refresh').disabled,true);
});
test('busy refresh prevents overlapping USB work, records and review; no auto transport retry',async()=>{
 const gate=deferred(),f=ui({read:()=>gate.promise}),pending=f.get('service-refresh').click();await f.get('service-refresh').click();await f.get('service-review').click();f.get('service-records').click();assert.equal(f.reads,1);assert.equal(f.reviews,0);assert.equal(f.records,0);gate.resolve(snapshot());await pending;f.get('service-records').click();assert.equal(f.records,1);f.setAllowed(false);assert.equal(f.get('service-refresh').disabled,true);assert.equal(f.get('service-copy').disabled,true);
});
test('review UI states actual unsupported-writer and verification limits using fixed text',async()=>{
 const f=ui({review:async()=>({version:'0.4.1-setup-dev',board:'lilygo-t-dongle-s3',sha256:'a'.repeat(64),bytes:1681344,signature_status:'Not verified',can_install:false,message:'SYNTHETIC arbitrary private details ignored'})});await f.get('service-review').click();const text=f.get('service-bundle').textContent;assert.match(text,/Device compatibility: not established/);assert.match(text,/Recovery backup: not verified here/);assert.match(text,/no integrated firmware writer/);assert.doesNotMatch(text,/SYNTHETIC|private details/);assert.equal(f.reads,0);
});
test('usb_diagnostics bridge accepts only a selected port, never file paths or credentials',async()=>{
 let request;const listeners=new Set(),native={request(raw){request=JSON.parse(raw);},response:{connect:f=>listeners.add(f),disconnect:f=>listeners.delete(f)},events:{connect(){},disconnect(){}}},bridge=new DesktopBridge(native);
 const p=bridge.request('usb_diagnostics',{port:'synthetic-port'});assert.equal(request.op,'usb_diagnostics');for(const listener of listeners)listener(JSON.stringify({id:request.id,ok:true,result:snapshot()}));await p;
 for(const args of [{},{port:''},{port:'synthetic-port',path:'/private'},{port:'synthetic-port',ownerKey:'SYNTHETIC'}])await assert.rejects(bridge.request('usb_diagnostics',args));bridge.close();
});
test('service markup preserves privacy boundaries and offers no fabricated install action',async()=>{
 const html=await readFile(new URL('../frontend/index.html',import.meta.url),'utf8');assert.match(html,/id="service-refresh"/);assert.match(html,/readonly spellcheck="false"/);assert.match(html,/no integrated firmware writer/);assert.match(html,/complete, reviewed versioned installer/);assert.doesNotMatch(html,/id="service-(?:install|flash|erase|reset)"/);const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(x=>x[1]);assert.equal(new Set(ids).size,ids.length);
});

test('Bluetooth cleanup guidance requires a recorded failed close and paused backups',()=>{
 const d=snapshot();d.lifecycle.ble_memory_restored=false;
 assert.equal(diagnosticAdvice(d).code,'local_ready','A historical failed close alone is not a current pause');
 d.status.backups_paused=true;d.backup.paused=true;d.backup.worker_phase=0;d.status.wifi='idle';
 const a=diagnosticAdvice(d);assert.equal(a.code,'bluetooth_cleanup_review');assert.match(a.detail,/last Bluetooth close.*memory.*Backups are paused/);assert.match(a.next,/Wait 30 seconds.*diagnose again.*safe report/);
 d.status.ble=true;assert.equal(diagnosticAdvice(d).code,'setup_open','A live setup session must not be called a new close failure');
 d.status.ble=false;d.lifecycle.ble_recovery_checked=false;assert.equal(diagnosticAdvice(d).code,'backup_paused','An unmeasured close cannot be called a failed memory proof');
});
test('diagnosis prioritizes reported storage, owner, memory and worker faults',()=>{
 for(const [edit,code]of [[d=>d.status.store_fault=true,'settings_review'],[d=>d.status.owner_ready=false,'owner_review'],[d=>d.memory.allocation_failure_seen=true,'memory_review'],[d=>d.backup.worker_error=-2201,'worker_review'],[d=>d.backup.worker_phase=5,'worker_review']]){
  const d=snapshot();d.status.ble=true;edit(d);assert.equal(diagnosticAdvice(d).code,code);
 }
});
test('missing network and cloud failures produce different, bounded next steps',()=>{
 const d=snapshot();d.status.wifi='connecting';assert.equal(diagnosticAdvice(d).code,'wifi_waiting');assert.match(diagnosticAdvice(d).next,/previously saved network or hotspot/);
 d.status.wifi='local_connected';d.backup.cloud_configured=false;assert.equal(diagnosticAdvice(d).code,'cloud_setup_needed');
 d.backup.cloud_configured=true;d.backup.error=-2402;const a=diagnosticAdvice(d);assert.equal(a.code,'backup_retry');assert.match(a.next,/cannot check the account or identify the cause/);assert.doesNotMatch(a.detail,/password|incorrect|internet/i);
});
test('incomplete scans and stopped workers cannot produce a ready diagnosis',()=>{
 for(const edit of [d=>d.backup.has_scan=false,d=>d.backup.scan_complete=false,d=>d.backup.worker_phase=0,d=>d.backup.worker_phase=1]){const d=snapshot();edit(d);assert.equal(diagnosticAdvice(d).code,'backup_waiting');}
 const d=snapshot();d.dashboard.dashboard_fault=true;assert.equal(diagnosticAdvice(d).code,'dashboard_review');
});
test('ready diagnosis preserves cloud and file-integrity limits and rejects extra private fields',()=>{
 const d=snapshot(),before=structuredClone(d),a=diagnosticAdvice(d);assert.equal(a.code,'local_ready');assert.match(a.next,/Cloud access and file integrity were not tested/);assert.deepEqual(d,before);
 d.status.ssid='SYNTHETIC-PRIVATE';assert.throws(()=>diagnosticAdvice(d));
});
test('service displays actionable Bluetooth guidance and clears it with the snapshot',async()=>{
 const d=snapshot();d.status.backups_paused=true;d.backup.paused=true;d.lifecycle.ble_memory_restored=false;
 const f=ui({read:async()=>d});await f.get('service-refresh').click();assert.equal(f.reads,1);assert.equal(f.get('service-attention').hidden,false);assert.match(f.get('service-attention').textContent,/Bluetooth cleanup needs review.*copy the safe report/);assert.equal(f.get('service-copy').disabled,false);
 f.panel.clear();assert.equal(f.get('service-attention').hidden,true);assert.equal(f.get('service-attention').textContent,'');assert.equal(f.get('service-copy').disabled,true);
});
