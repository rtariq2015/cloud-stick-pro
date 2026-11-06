// Native Linux transport for the SAME production client/Security2 code.
// Private input arrives on stdin under a no-swap scope; output is allowlisted.
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';
import {ManagementConnection,WifiSetupClient,DiagnosticLogClient,PublicWifiClient,SERVICE} from '../../phone/wifi-setup/connection.mjs';
const emit=value=>process.stdout.write(JSON.stringify(value)+'\n');
const input=createInterface({input:process.stdin});
const line=await new Promise(resolve=>input.once('line',resolve));input.close();
if(typeof line!=='string'||line.length>4096)throw Error('Private runner input missing');
let settings;try{settings=JSON.parse(line);}catch{throw Error('Invalid private runner input');}
if(!['identity','scan','check','save','hold','backup','activity','portal'].includes(settings.mode)||!/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(settings.address)||typeof settings.ownerKey!=='string'||settings.ownerKey.length!==32)throw Error('Invalid private runner parameters');
const child=spawn('/usr/bin/python3',[fileURLToPath(new URL('./bluez_bridge.py',import.meta.url)),'--address',settings.address],{stdio:['pipe','pipe','pipe']});
let id=0;const pending=new Map();let stderrSeen=false;
child.stderr.on('data',()=>{stderrSeen=true;});
const rpc=createInterface({input:child.stdout});
rpc.on('line',line=>{
 let r;try{r=JSON.parse(line);}catch{return;}
 const job=pending.get(r.id);if(!job)return;pending.delete(r.id);clearTimeout(job.timer);
 if(r.ok)job.resolve(r);else{const e=Error('Native GATT operation failed');e.code=r.error_name??r.error_type??'failure';job.reject(e);}
});
const call=(op,args={})=>new Promise((resolve,reject)=>{
 if(child.exitCode!==null||child.signalCode!==null||child.stdin.destroyed){reject(Error('NativeGatt:closed'));return;}
 const identifier=++id,timer=setTimeout(()=>{pending.delete(identifier);reject(Error('NativeGatt:timeout'));},20000);
 pending.set(identifier,{resolve,reject,timer});child.stdin.write(JSON.stringify({id:identifier,op,...args})+'\n');
});
child.on('exit',()=>{for(const job of pending.values()){clearTimeout(job.timer);job.reject(Error('NativeGatt:transport-exit'));}pending.clear();});
child.stdin.on('error',()=>{for(const job of pending.values()){clearTimeout(job.timer);job.reject(Error('NativeGatt:pipe-closed'));}pending.clear();});
const events=new Map();
const gatt={connected:false,async connect(){await call('connect');this.connected=true;return {async getPrimaryService(uuid){
 if(uuid!==SERVICE)throw Error('Unexpected service');
 return {async getCharacteristic(uuid){return {
  async writeValueWithResponse(data){await call('write',{uuid,hex:Buffer.from(data).toString('hex')});},
  async readValue(){const r=await call('read',{uuid});const b=Buffer.from(r.hex,'hex');return new DataView(b.buffer,b.byteOffset,b.byteLength);}
 };}};
}};},disconnect(){this.connected=false;call('disconnect').catch(()=>{});}};
const device={gatt,addEventListener:(event,callback)=>events.set(event,callback),removeEventListener:event=>events.delete(event)};
let lastState='starting';
const connection=new ManagementConnection({bluetooth:{async requestDevice(){return device;}},secureContext:true,ownerKey:settings.ownerKey,
 onState:({state})=>{lastState=state;emit({event:'connection_state',state});}});
settings.ownerKey='';
try{
 const info=await connection.connect();emit({event:'authenticated',...(['activity','portal'].includes(settings.mode)?{}:{firmware:info.firmware}),synthetic:info.synthetic});
 const flow=new WifiSetupClient(connection),state=await flow.status();
 emit({event:'initial_status',revision:state.revision,wifi:state.wifi,durable:state.durable});
 if(settings.mode==='backup'){
  if(info.backup_status!==true)throw Error('Backup status capability missing');
  const backup=await flow.backupStatus();
  emit({event:'backup_status',...backup});
 }
 if(settings.mode==='activity'){
  if(info.event_log!==true)throw Error('Activity log capability missing');
  const log=await new DiagnosticLogClient(connection).read();
  // The strict client returns only numeric records and boolean persistence facts.
  emit({event:'activity_log',...log});
 }
 if(settings.mode==='portal'){
  if(info.public_wifi!==true)throw Error('Public Wi-Fi capability missing');
  const profiles=await flow.profiles(),current=await flow.status();
  const selected=profiles.find(profile=>profile.id===current.selected_profile&&profile.enabled);
  if(!selected||!current.durable||current.store_fault||current.wifi!=='local_connected'||
     current.wifi_profile!==selected.id||current.applied_revision!==current.revision)throw Error('Saved active Wi-Fi is not ready for handoff');
  const portal=new PublicWifiClient(connection);
  await portal.prepare({profile:selected.id,revision:current.revision});
  // No grant, AP credentials or profile name leave this process. The complete
  // authenticated result (including polling) is read before explicit teardown.
  await portal.handoff();connection.disconnect();await call('disconnect');
  emit({event:'portal_handoff',ack_read:true,disconnect_requested:true});
 }
 if(settings.mode==='hold'){
  const started=performance.now();let expired=false;
  while(performance.now()-started<135000){
   await new Promise(resolve=>setTimeout(resolve,2000));
   try{await flow.status();}
   catch(error){
    const elapsed=Math.round(performance.now()-started);
    if(elapsed>=100000&&['DisconnectedError','org.bluez.Error.NotConnected','org.bluez.Error.Failed'].includes(error?.code)){
     expired=true;emit({event:'window_closed',elapsed_ms:elapsed});break;
    }
    throw error;
   }
  }
  if(!expired)throw Error('Expected physical setup window did not close');
 }
 if(!['identity','hold','backup','activity','portal'].includes(settings.mode)){
  const rows=await flow.scan();emit({event:'scan_complete',networks:rows.length});
 }
 if(['check','save'].includes(settings.mode)){
  const profiles=await flow.profiles(),selected=profiles.find(p=>p.id===state.selected_profile&&p.enabled);
  if(!selected)throw Error('No active saved profile');
  await flow.stage({profile:selected.id,ssid:selected.ssid,open:selected.open,keep:!selected.open,priority:selected.priority,enabled:selected.enabled});
  const checked=await flow.check({onStatus:s=>emit({event:'check_state',wifi:s.wifi,phase:s.phase,error:s.check_error??0})});
  emit({event:'check_complete',wifi:checked.wifi,internet_verified:checked.internet_verified});
  if(settings.mode==='save'){const saved=await flow.save();emit({event:'save_complete',revision:saved.revision,durable:saved.durable});}
 }
 emit({event:'native_test_complete',mode:settings.mode});
}catch(error){
 // Never serialize messages, stack traces, credentials, SSIDs or decrypted frames.
 const result={event:'native_test_failed',error_type:error?.constructor?.name??'Error',phase:lastState};
 if(typeof error?.code==='string'&&/^[A-Za-z0-9_.-]{1,128}$/.test(error.code))result.transport_error=error.code;
 emit(result);process.exitCode=1;
}finally{
 connection.disconnect();await call('disconnect').catch(()=>{});child.stdin.end();
 if(child.exitCode===null&&child.signalCode===null)await new Promise(resolve=>child.once('exit',resolve));rpc.close();
 if(stderrSeen){emit({event:'native_transport_stderr_present'});process.exitCode=1;}
}
