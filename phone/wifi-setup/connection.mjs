import {Security2Client,ProtocolError,validateOwnerLogin,createOwnerRecord,OwnerRecordError} from './security2.mjs';
export {validateOwnerLogin,OwnerRecordError};
export const SERVICE='021a9004-0382-4aea-bff4-6b3f1c5adfb4';
export const SESSION='021aff51-0382-4aea-bff4-6b3f1c5adfb4';
export const INFO='021aff52-0382-4aea-bff4-6b3f1c5adfb4';
export const WIFI='021aff54-0382-4aea-bff4-6b3f1c5adfb4';
export const VERSION='021aff53-0382-4aea-bff4-6b3f1c5adfb4';
const encode=new TextEncoder(),decode=new TextDecoder('utf-8',{fatal:true});
function packet(view){const out=view instanceof Uint8Array?view:new Uint8Array(view.buffer,view.byteOffset,view.byteLength);if(out.length>512)throw new ProtocolError('Device response is too large');return out.slice();}
export function validateMetadata(value){
 if(!value||value.prov?.sec_ver!==2||value.prov?.sec_patch_ver!==1||value.csp?.milestone!==2||value.csp?.synthetic!==false||value.csp?.owner!=='card'||value.csp?.wifi!==true||typeof value.csp?.cloud!=='boolean')throw new ProtocolError('This device does not offer the reviewed private-owner Wi-Fi setup protocol');
 if(value.csp.cloud===true?value.csp.cloud_protocol!==1:Object.hasOwn(value.csp,'cloud_protocol'))throw new ProtocolError('Unsupported private cloud setup protocol');
 if(Object.hasOwn(value.csp,'owner_login')&&typeof value.csp.owner_login!=='boolean')throw new ProtocolError('Invalid owner login capability');
 if(value.csp.owner_login===true?value.csp.owner_login_protocol!==1:Object.hasOwn(value.csp,'owner_login_protocol'))throw new ProtocolError('Unsupported owner login protocol');
 if(Object.hasOwn(value.csp,'backup_status')&&typeof value.csp.backup_status!=='boolean')throw new ProtocolError('Invalid backup capability');
 if(Object.hasOwn(value.csp,'public_wifi')&&typeof value.csp.public_wifi!=='boolean')throw new ProtocolError('Invalid public Wi-Fi capability');
 if(value.csp.public_wifi===true?value.csp.public_wifi_protocol!==1:Object.hasOwn(value.csp,'public_wifi_protocol'))throw new ProtocolError('Unsupported public Wi-Fi protocol');
 if(Object.hasOwn(value.csp,'event_log')&&typeof value.csp.event_log!=='boolean')throw new ProtocolError('Invalid activity-log capability');
 if(value.csp.event_log===true?value.csp.event_log_protocol!==1:Object.hasOwn(value.csp,'event_log_protocol'))throw new ProtocolError('Unsupported activity-log protocol');
 if(Object.hasOwn(value.csp,'file_history')&&typeof value.csp.file_history!=='boolean')throw new ProtocolError('Invalid file-record capability');
 if(value.csp.file_history===true?value.csp.file_history_protocol!==1:Object.hasOwn(value.csp,'file_history_protocol'))throw new ProtocolError('Unsupported file-record protocol');
 if(Object.hasOwn(value.csp,'ble_wake')&&typeof value.csp.ble_wake!=='boolean')throw new ProtocolError('Invalid Wi-Fi wake capability');
 if(value.csp.ble_wake===true?value.csp.ble_wake_protocol!==1:Object.hasOwn(value.csp,'ble_wake_protocol'))throw new ProtocolError('Unsupported Wi-Fi wake protocol');
 return value;
}
export function validateInfo(value,id){
 const allowed=['v','id','ok','model','device_id','firmware','wifi_connected','backups_paused','synthetic','backup_status'];
 if(!value||Object.keys(value).some(k=>!allowed.includes(k))||value.v!==1||value.id!==id||value.ok!==true||value.synthetic!==false||value.backups_paused!==true||typeof value.wifi_connected!=='boolean')throw new ProtocolError('Invalid or unexpected device information');
 for(const k of ['model','device_id','firmware'])if(typeof value[k]!=='string'||value[k].length<1||value[k].length>96||/[\x00-\x1f\x7f]/.test(value[k]))throw new ProtocolError('Invalid device identification');
 if(Object.hasOwn(value,'backup_status')&&typeof value.backup_status!=='boolean')throw new ProtocolError('Invalid backup capability');
 return value;
}
export function connectionError(error){
 if(error?.name==='NotFoundError')return 'No device selected. Hold the dongle button to open its setup window, then try again.';
 if(error?.name==='SecurityError')return 'Bluetooth access was blocked. Open this page over trusted HTTPS in Chrome and allow Nearby devices access.';
 if(error?.name==='NetworkError')return 'The Bluetooth connection ended. Stay nearby, reopen the setup window and reconnect.';
 if(error instanceof ProtocolError)return error.message;
 return 'The connection could not be completed. Reopen the setup window and try again.';
}
export class ManagementConnection {
 #bluetooth;#secure;#device=null;#security=null;#generation=0;#id=0;#busy=false;#timeout;#timer=null;#onState;#securityFactory;#session=null;#info=null;#pendingConnect=null;#wifi=null;#ownerClient=null;#bleWakeCapable=false;#backupCapable=false;#publicWifiCapable=false;#eventLogCapable=false;#fileHistoryCapable=false;#cloudCapable=false;#ownerLoginCapable=false;#rekeyRequired=false;#loginReserved=false;#loginStarted=false;#loginCommitSent=false;
 constructor({bluetooth=globalThis.navigator?.bluetooth,secureContext=globalThis.isSecureContext,onState=()=>{},timeoutMs=15000,sessionTimeoutMs=120000,securityFactory,ownerKey,username,password}={}){
  this.#bluetooth=bluetooth;this.#secure=secureContext;this.#onState=onState;this.#timeout=timeoutMs;this.sessionTimeoutMs=sessionTimeoutMs;
  if(securityFactory)this.#securityFactory=securityFactory;
  else{
   if(ownerKey!==undefined){if(typeof ownerKey!=='string'||ownerKey.length<24||ownerKey.length>128)throw new ProtocolError('Enter the fixed private key from your owner card.');username='csp-owner';password=ownerKey;}
   else validateOwnerLogin(username,password);
   this.#ownerClient=new Security2Client(username,password);username='';password='';ownerKey='';
   this.#securityFactory=()=>{if(!this.#ownerClient)throw new ProtocolError('Enter your login again for a new connection.');const client=this.#ownerClient;this.#ownerClient=null;return client;};
  }
 }
 #notify(state,message,info){this.#onState({state,message,info});}
 #assert(generation){if(generation!==this.#generation)throw new ProtocolError('Connection was closed');}
 async #bounded(promise,generation){
  let timer;try{const result=await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new ProtocolError('Connection timed out. Reopen the physical setup window and reconnect.')),this.#timeout);})]);this.#assert(generation);return result;}finally{clearTimeout(timer);}
 }
 async #exchange(characteristic,payload,generation){
  if(payload.length>512)throw new ProtocolError('The Bluetooth attribute value is too large.');
  await this.#bounded(characteristic.writeValueWithResponse(payload),generation);
  return packet(await this.#bounded(characteristic.readValue(),generation));
 }
 async connect(){
  if(this.#busy){const error=new ProtocolError('A Bluetooth operation is still finishing. Wait a moment, then tap Find Cloud Stick again.');this.#notify('error',error.message);throw error;}
  if(this.#pendingConnect){const error=new ProtocolError('The previous Bluetooth connection is still closing. Wait a moment, then try again.');this.#notify('error',error.message);throw error;}
  this.disconnect(false);const generation=this.#generation;this.#busy=true;
  try{
   if(!this.#secure||!this.#bluetooth?.requestDevice)throw new ProtocolError('Open this page over trusted HTTPS in Android Chrome with Bluetooth enabled.');
   this.#notify('choosing','Choose the Cloud Stick shown in the Bluetooth window.');
   // Must run directly from the user's click; no prior async operation.
   this.#device=await this.#bounded(this.#bluetooth.requestDevice({filters:[{namePrefix:'CSP-W2-'}],optionalServices:[SERVICE]}),generation);
   this.#device.addEventListener('gattserverdisconnected',this.#disconnected);
   this.#notify('connecting','Connecting to the nearby dongle…');
   const connectingDevice=this.#device,pending=connectingDevice.gatt.connect();this.#pendingConnect=pending;
   // A browser connect promise can complete after our timeout/cancellation. Keep
   // this attempt reserved until settlement, then close its late radio link.
   // Retrying sooner could confuse an old result with a newer same-device link.
   const settled=()=>{if(this.#pendingConnect===pending)this.#pendingConnect=null;};
   pending.then(()=>{if(generation!==this.#generation){try{connectingDevice.gatt.disconnect();}catch{}}settled();},settled);
   const server=await this.#bounded(pending,generation),service=await this.#bounded(server.getPrimaryService(SERVICE),generation);
   const version=await this.#bounded(service.getCharacteristic(VERSION),generation);
   // NimBLE reads return the response cached by the last nonempty write. Its
   // cache is empty initially and cleared on disconnect; use the official probe.
   const metadata=validateMetadata(JSON.parse(decode.decode(await this.#exchange(version,encode.encode('---'),generation))));
   this.#bleWakeCapable=metadata.csp.ble_wake===true&&metadata.csp.ble_wake_protocol===1;
   this.#backupCapable=metadata.csp.backup_status===true;
   this.#publicWifiCapable=metadata.csp.public_wifi===true&&metadata.csp.public_wifi_protocol===1;
   this.#eventLogCapable=metadata.csp.event_log===true&&metadata.csp.event_log_protocol===1;
   this.#fileHistoryCapable=metadata.csp.file_history===true&&metadata.csp.file_history_protocol===1;
   this.#cloudCapable=metadata.csp.cloud===true&&metadata.csp.cloud_protocol===1;
   this.#ownerLoginCapable=metadata.csp.owner_login===true&&metadata.csp.owner_login_protocol===1;
   this.#session=await this.#bounded(service.getCharacteristic(SESSION),generation);this.#info=await this.#bounded(service.getCharacteristic(INFO),generation);
   this.#wifi=await this.#bounded(service.getCharacteristic(WIFI),generation);
   this.#security=this.#securityFactory();this.#notify('authenticating','Checking your private owner key…');
   const first=await this.#bounded(this.#security.start(),generation);
   const challenge=await this.#exchange(this.#session,first,generation);
   const proof=await this.#bounded(this.#security.challenge(challenge),generation);
   this.#security.finish(await this.#exchange(this.#session,proof,generation));
   const result=await this.#readInfo(generation);this.#notify('connected','Private owner authenticated. Wi-Fi setup is available; backups are paused.',result);
   this.#timer=setTimeout(()=>{this.disconnect(false);this.#notify('disconnected','The two-minute phone session ended. Reconnect when you need it.');},this.sessionTimeoutMs);
   return result;
  }catch(error){this.#ownerClient?.destroy();this.#ownerClient=null;if(generation===this.#generation){this.disconnect(false);this.#notify('error',connectionError(error));}throw error;}
  finally{this.#busy=false;}
 }
 async #readInfo(generation){
  const id=++this.#id,request=encode.encode(JSON.stringify({v:1,id,op:'info'}));
  const encrypted=await this.#bounded(this.#security.encrypt(request),generation);
  const answer=await this.#exchange(this.#info,encrypted,generation);
  const plain=await this.#bounded(this.#security.decrypt(answer),generation);
  const info=validateInfo(JSON.parse(decode.decode(plain)),id);
  // Keep the authenticated wire Info shape compatible with cached M2 clients.
  // This local capability annotation is exposed only after Security2 succeeds.
  return {...info,...(this.#bleWakeCapable?{ble_wake:true}:{}),...(this.#backupCapable?{backup_status:true}:{}),...(this.#publicWifiCapable?{public_wifi:true}:{}),...(this.#eventLogCapable?{event_log:true}:{}),...(this.#fileHistoryCapable?{file_history:true}:{}),...(this.#cloudCapable?{cloud_setup:true}:{}),...(this.#ownerLoginCapable?{owner_login:true}:{})};
 }
 async refresh(){
  if(this.#rekeyRequired)throw new ProtocolError('Login changed. Disconnect and reconnect with your new login.');
  if(this.#loginReserved)throw new ProtocolError('A login change is in progress. Finish or cancel it before refreshing.');
  if(!this.#security){const error=new ProtocolError('No authenticated session. Tap Find Cloud Stick to connect again.');this.#notify('disconnected',error.message);throw error;}
  if(this.#busy)throw new ProtocolError('A Bluetooth operation is still finishing. Wait a moment, then try again.');
  this.#busy=true;const generation=this.#generation;
  try{const info=await this.#readInfo(generation);this.#notify('connected','Device information refreshed.',info);return info;}
  catch(error){if(generation===this.#generation){this.disconnect(false);this.#notify('error',connectionError(error));}throw error;}finally{this.#busy=false;}
 }
 async #wifiRequest(operation,args,generation){
  if(!this.#security||!this.#wifi)throw new ProtocolError('Reconnect with your private owner key.');
  if(this.#id>=0x7fffffff)throw new ProtocolError('Request limit reached. Reconnect.');
  const id=++this.#id,plain=encode.encode(JSON.stringify({v:2,id,op:operation,...args}));
  if(plain.length>496){plain.fill(0);throw new ProtocolError('The settings message is too large.');}
  let encrypted;
  try{encrypted=await this.#bounded(this.#security.encrypt(plain),generation);}finally{plain.fill(0);}
  const response=await this.#exchange(this.#wifi,encrypted,generation);encrypted.fill(0);
  const decoded=await this.#bounded(this.#security.decrypt(response),generation);
  let data;try{data=JSON.parse(decode.decode(decoded));}finally{decoded.fill(0);}
  if(!data||data.v!==2||data.id!==id||typeof data.ok!=='boolean'||
    Object.keys(data).some(k=>!['v','id','ok','job','pending','data','error'].includes(k)))throw new ProtocolError('Invalid Wi-Fi command response.');
  if(!data.ok)throw new ProtocolError(data.error==='busy'?'The previous device operation is still finishing.':'The device rejected this request. Reconnect to check its saved settings.');
  if(!Number.isInteger(data.job)||data.job<1||data.job>0xffffffff||typeof data.pending!=='boolean')throw new ProtocolError('Invalid Wi-Fi job identifier.');
  return data;
 }
 async command(operation,args={}){
  if(this.#rekeyRequired)throw new ProtocolError('Login changed. Disconnect and reconnect with your new login.');
  const loginOperation=['login_begin','login_stage','login_commit','login_abort'].includes(operation);
  if(this.#loginCommitSent&&loginOperation)throw new ProtocolError('A login save is pending or needs reconnection. No further login commands are accepted.');
  if(this.#loginReserved&&!loginOperation)throw new ProtocolError('A login change is in progress. Finish or cancel it first.');
  if(!this.#security||!this.#wifi)throw new ProtocolError('Connect to your Cloud Stick first.');
  if(this.#busy)throw new ProtocolError('A device operation is still finishing.');
  if((!['list','scan','scan_page','begin','stage','forget','check','status','commit','abort'].includes(operation)&&
      !(operation==='wake_pair'&&this.#bleWakeCapable)&&
      !(operation==='backup_status'&&this.#backupCapable)&&
      !(operation==='event_log'&&this.#eventLogCapable)&&
      !(['history_status','history_row'].includes(operation)&&this.#fileHistoryCapable)&&
      !(['cloud_status','cloud_begin','cloud_stage','cloud_commit','cloud_abort'].includes(operation)&&this.#cloudCapable)&&
      !(['login_begin','login_stage','login_commit','login_abort'].includes(operation)&&this.#ownerLoginCapable)&&
      !(['portal_begin','portal_handoff','portal_cancel','portal_status'].includes(operation)&&this.#publicWifiCapable))||
    !args||Object.keys(args).some(k=>['v','id','op'].includes(k)))throw new ProtocolError('Unsupported Wi-Fi command.');
  if(operation==='login_begin'){
   if(this.#loginStarted)throw new ProtocolError('A login change is already in progress.');
   this.#loginReserved=true;this.#loginStarted=true;
  }else if(loginOperation&&!this.#loginStarted)throw new ProtocolError('Start a login change before sending its fields.');
  if(operation==='login_commit')this.#loginCommitSent=true;
  this.#busy=true;const generation=this.#generation,deadline=Date.now()+this.#timeout;
  try{
   let answer=await this.#wifiRequest(operation,args,generation);const job=answer.job;
   while(answer.pending){
    if(Date.now()>=deadline)throw new ProtocolError('The device did not finish in time. Reconnect to check saved settings.');
    await this.#bounded(new Promise(resolve=>setTimeout(resolve,100)),generation);
    answer=await this.#wifiRequest('result',{job},generation);
    if(answer.job!==job)throw new ProtocolError('A response belongs to a different operation.');
   }
   if(!answer.data||typeof answer.data!=='object'||Array.isArray(answer.data))throw new ProtocolError('Invalid Wi-Fi operation result.');
   if(operation==='login_commit'&&answer.data.ok===true&&answer.data.durable===true&&answer.data.reconnect_required===true)this.#rekeyRequired=true;
   if((operation==='login_begin'&&answer.data.ok===false)||(operation==='login_abort'&&answer.data.ok===true)){this.#loginReserved=false;this.#loginStarted=false;}
   return answer.data;
  }catch(error){if(generation===this.#generation){this.disconnect(false);this.#notify('error',connectionError(error));}throw error;}
  finally{this.#busy=false;}
 }
 async reserveOwnerLogin(){
  if(!this.#security||!this.#wifi||!this.#ownerLoginCapable)throw new ProtocolError('Connect to a stick that supports private login changes.');
  if(this.#loginReserved||this.#rekeyRequired)throw new ProtocolError('A login change is already in progress.');
  this.#loginReserved=true;const generation=this.#generation,deadline=Date.now()+this.#timeout;
  try{
   // Stop new application work immediately, but let the already admitted job
   // read its exact result before starting a login transaction. No task/link
   // is cancelled to acquire this reservation.
   while(this.#busy){
    if(Date.now()>=deadline)throw new ProtocolError('The current device job is still finishing. Reconnect before changing login.');
    await this.#bounded(new Promise(resolve=>setTimeout(resolve,20)),generation);
   }
   this.#assert(generation);
  }catch(error){if(generation===this.#generation){this.#loginReserved=false;this.#loginStarted=false;}throw error;}
 }
 #disconnected=()=>{this.disconnect(false);this.#notify('disconnected','Bluetooth disconnected. The dongle keeps your files. Reconnect when ready.');};
 disconnect(notify=true){
  if(notify){this.#ownerClient?.destroy();this.#ownerClient=null;}
  ++this.#generation;this.#bleWakeCapable=false;this.#backupCapable=false;this.#publicWifiCapable=false;this.#eventLogCapable=false;this.#fileHistoryCapable=false;this.#cloudCapable=false;this.#ownerLoginCapable=false;this.#rekeyRequired=false;this.#loginReserved=false;this.#loginStarted=false;this.#loginCommitSent=false;clearTimeout(this.#timer);this.#timer=null;this.#security?.destroy();this.#security=null;this.#session=this.#info=this.#wifi=null;this.#id=0;
  const device=this.#device;this.#device=null;
  if(device){device.removeEventListener('gattserverdisconnected',this.#disconnected);try{device.gatt.disconnect();}catch{}}
  if(notify)this.#notify('disconnected','Disconnected. You can reconnect through a new setup window.');
 }
}
// One authenticated numeric event per page; never fetch files or unbounded text.
// The paused-session snapshot must remain coherent across the complete read.
export class DiagnosticLogClient {
 #connection;
 constructor(connection){this.#connection=connection;}
 #invalid(){this.#connection.disconnect?.();throw new ProtocolError('Device activity changed or was invalid. Reconnect to read it again.');}
 async read(){
  const fields=['ok','events','next','more','newest','persisted','loaded','dirty','corrupt','io_failed','exhausted'];
  const flags=['loaded','dirty','corrupt','io_failed','exhausted'];
  const uint=value=>Number.isSafeInteger(value)&&value>=0&&value<=0xffffffff;
  const events=[];let cursor=0,snapshot=null;
  for(let page=0;page<12;++page){
   const data=await this.#connection.command('event_log',{cursor});
   if(!data||typeof data!=='object'||Array.isArray(data)||fields.some(key=>!Object.hasOwn(data,key))||
      Object.keys(data).some(key=>!fields.includes(key))||data.ok!==true||!Array.isArray(data.events)||data.events.length>1||
      !uint(data.next)||!uint(data.newest)||!uint(data.persisted)||data.persisted>data.newest||data.newest<cursor||
      typeof data.more!=='boolean'||flags.some(key=>typeof data[key]!=='boolean'))this.#invalid();
   const current={newest:data.newest,persisted:data.persisted,loaded:data.loaded,dirty:data.dirty,corrupt:data.corrupt,io_failed:data.io_failed,exhausted:data.exhausted};
   if(snapshot&&Object.keys(current).some(key=>current[key]!==snapshot[key]))this.#invalid();snapshot=current;
   if(data.events.length){
    const event=data.events[0],keys=['s','u','t','k','c'];
    if(!event||typeof event!=='object'||Array.isArray(event)||keys.some(key=>!Object.hasOwn(event,key))||Object.keys(event).some(key=>!keys.includes(key))||
       !uint(event.s)||event.s<=cursor||event.s>data.newest||(cursor>0&&event.s!==cursor+1)||!uint(event.u)||
       !Number.isSafeInteger(event.t)||(event.t!==0&&(event.t<1767225600||event.t>253402300799))||
       !Number.isInteger(event.k)||event.k<1||event.k>8||!Number.isInteger(event.c)||event.c< -0x80000000||event.c>0x7fffffff||
       (event.k===4&&(event.c<0||event.c>128))||data.next!==event.s)this.#invalid();
    events.push({s:event.s,u:event.u,t:event.t,k:event.k,c:event.c});
   }else if(data.next!==cursor||data.more||cursor!==data.newest)this.#invalid();
   if(data.more?(data.next<=cursor||data.next>=data.newest):data.next!==data.newest)this.#invalid();
   cursor=data.next;if(!data.more)return {...snapshot,events};
  }
  this.#invalid();
 }
}

const historyUint=value=>Number.isInteger(value)&&value>=0&&value<=0xffffffff;
const historyDecimal=value=>typeof value==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(value)&&BigInt(value)<=0xffffffffffffffffn;
function historyPath(value){
 if(!value||encode.encode(value).length>240||value.startsWith('/')||value.endsWith('/')||/[\x00-\x1f\x7f\\:]/.test(value))return false;
 const parts=value.split('/');return parts.length<=8&&parts.every(part=>part&&part!=='.'&&part!=='..')&&
  !['.cloud-stick','System Volume Information','$RECYCLE.BIN'].includes(parts[0])&&!parts[0].startsWith('._csp_uploads_');
}
// The device keeps at most eight paused-session receipt summaries. Hex is only
// a standard byte encoding so UTF-8 characters can cross fragment boundaries;
// ownership/authentication and encryption remain the official Security2 flow.
export class FileHistoryClient {
 #connection;
 constructor(connection){this.#connection=connection;}
 #invalid(){this.#connection.disconnect?.();throw new ProtocolError('Invalid saved file record. Reconnect to read a new snapshot.');}
 async read(){
  const status=await this.#connection.command('history_status');
  const keys=['ok','g','n','matched','when','loaded','complete','limited','corrupt','io_failed','allocation_failed','exhausted'];
  if(!status||typeof status!=='object'||Array.isArray(status)||keys.some(key=>!Object.hasOwn(status,key))||Object.keys(status).some(key=>!keys.includes(key))||
     status.ok!==true||!historyUint(status.g)||!Number.isInteger(status.n)||status.n<0||status.n>8||
     !Number.isInteger(status.matched)||status.matched<status.n||status.matched>128||!historyDecimal(status.when)||
     ['loaded','complete','limited','corrupt','io_failed','allocation_failed','exhausted'].some(key=>typeof status[key]!=='boolean')||
     ((!status.loaded||status.allocation_failed||status.exhausted||status.g===0)&&status.n!==0))this.#invalid();
  const rows=[];
  for(let index=0;index<status.n;++index){
   let offset=0,metadata=null;const bytes=new Uint8Array(240);
   try{
    for(let page=0;page<4;++page){
     const data=await this.#connection.command('history_row',{g:status.g,i:index,o:offset});
     const fields=['ok','g','i','n','b','s','v','r','e','a','p','o','x','m','h'];
     if(!data||typeof data!=='object'||Array.isArray(data)||fields.some(key=>!Object.hasOwn(data,key))||Object.keys(data).some(key=>!fields.includes(key))||
        data.ok!==true||data.g!==status.g||data.i!==index||data.n!==status.n||!historyDecimal(data.b)||!historyDecimal(data.v)||!historyDecimal(data.r)||
        !Number.isInteger(data.s)||data.s<0||data.s>2||!Number.isInteger(data.e)||data.e< -0x80000000||data.e>0x7fffffff||!historyUint(data.a)||
        !Number.isInteger(data.p)||data.p<1||data.p>240||data.o!==offset||!Number.isInteger(data.x)||data.x<offset||data.x>Math.min(data.p,offset+64)||
        typeof data.m!=='boolean'||data.m!==(data.x<data.p)||typeof data.h!=='string'||!/^(?:[0-9a-f]{2}){0,64}$/.test(data.h)||
        data.h.length!==(data.x-offset)*2||(data.m&&data.x===offset))this.#invalid();
     const current={bytes:data.b,state:data.s,verified:data.v,retry:data.r,error:data.e,attempts:data.a,path_bytes:data.p};
     if(metadata&&Object.keys(current).some(key=>current[key]!==metadata[key]))this.#invalid();metadata=current;
     for(let i=0;i<data.h.length;i+=2)bytes[offset+i/2]=parseInt(data.h.slice(i,i+2),16);
     offset=data.x;
     if(!data.m){
      const path=decode.decode(bytes.subarray(0,offset));if(!historyPath(path)||encode.encode(path).length!==metadata.path_bytes)this.#invalid();
      rows.push({...metadata,path});break;
     }
    }
    if(rows.length!==index+1)this.#invalid();
   }catch(error){if(error instanceof ProtocolError){this.#connection.disconnect?.();throw error;}this.#invalid();}
   finally{bytes.fill(0);}
  }
  return {...status,rows};
 }
}
const byteLength=s=>encode.encode(s).length;
const revision=s=>typeof s==='string'&&/^[1-9][0-9]{0,19}$/.test(s)&&BigInt(s)<=0xffffffffffffffffn;
// Only the narrow, authenticated preparation/handoff protocol lives here. The
// browser never proxies a venue page or sends the private key to an access point.
export class PublicWifiClient {
 #connection;#prepared=null;
 constructor(connection){this.#connection=connection;}
 #invalid(){this.#prepared=null;this.#connection.disconnect?.();throw new ProtocolError('Invalid sign-in handoff. Reconnect to check the stick.');}
 async prepare({profile,revision:current}){
  if(!Number.isInteger(profile)||profile<1||profile>0xffffffff||!revision(current))throw new WifiInputError(3);
  this.#prepared=null;
  const data=result(await this.#connection.command('portal_begin',{profile,revision:current}));
  const allowed=['ok','phase','grant','ap_ssid','ap_password','profile','revision','window_seconds','handoff_ms','next'];
  if(Object.keys(data).some(k=>!allowed.includes(k))||data.phase!=='prepared'||!revision(data.grant)||
     data.profile!==profile||data.revision!==current||typeof data.ap_ssid!=='string'||!/^CSP-Guest-[0-9a-f]{6}$/.test(data.ap_ssid)||
     data.ap_password!=='admin123'||data.window_seconds!==300||data.handoff_ms!==30000||data.next!=='handoff_then_disconnect')this.#invalid();
  this.#prepared=Object.freeze({...data});return this.#prepared;
 }
 async handoff(){
  const prepared=this.#prepared;if(!prepared)throw new ProtocolError('Prepare the sign-in connection first.');
  // Single-use locally as well as on the device. Never retry an uncertain ACK.
  this.#prepared=null;
  const data=result(await this.#connection.command('portal_handoff',{
   profile:prepared.profile,revision:prepared.revision,grant:prepared.grant}));
  const allowed=['ok','phase','remaining_ms','next','portal_error','attempt_dav_read_verified'];
  if(Object.keys(data).some(k=>!allowed.includes(k))||data.phase!=='disconnect_to_continue'||
     !Number.isInteger(data.remaining_ms)||data.remaining_ms<1||data.remaining_ms>10000||
     data.next!=='read_result_then_disconnect'||data.portal_error!==0||data.attempt_dav_read_verified!==false)this.#invalid();
  // The caller paints the already-received public instructions, then explicitly
  // disconnects. Completing this method alone does not claim the AP has started.
  return {ap_ssid:prepared.ap_ssid,ap_password:prepared.ap_password,window_seconds:prepared.window_seconds};
 }
 async cancel(){
  const prepared=this.#prepared;this.#prepared=null;if(!prepared)return;
  const data=result(await this.#connection.command('portal_cancel',{grant:prepared.grant}));
  const allowed=['ok','phase','portal_error','remaining_ms','attempt_dav_read_verified'];
  if(Object.keys(data).some(k=>!allowed.includes(k))||data.phase!=='complete'||
     data.portal_error!==6||data.remaining_ms!==0||data.attempt_dav_read_verified!==false)this.#invalid();
 }
}
export class WifiInputError extends Error {
 constructor(code){super('Wi-Fi settings were not accepted.');this.code=code;}
}
function result(data){if(data?.ok!==true)throw new WifiInputError(Number.isInteger(data?.error)?data.error:15);return data;}
function network(row){
 if(!row||typeof row.ssid!=='string'||!byteLength(row.ssid)||byteLength(row.ssid)>32||/[\x00-\x1f\x7f]/.test(row.ssid)||
   decode.decode(encode.encode(row.ssid))!==row.ssid||
   typeof row.open!=='boolean'||typeof row.supported!=='boolean'||!Number.isInteger(row.rssi)||row.rssi< -128||row.rssi>127)
   throw new ProtocolError('Invalid network list.');return row;
}
export class WifiSetupClient {
 #connection;#revision=null;#transaction=null;#edit=0;#profile=0;
 constructor(connection){this.#connection=connection;}
 #protocol(message){this.#connection.disconnect?.();throw new ProtocolError(message);}
 #network(row){try{return network(row);}catch{this.#protocol('Invalid network list.');}}
  #scanReady(page){
   // The firmware shares radio status with connection validation: LocalOnly20
   // is a healthy DHCP outcome after a connected scan, not a scan failure.
   // Require completed results and a healthy state; never accept20 on failure.
   return page.has_scan===true&&(['idle','local_connected'].includes(page.wifi))&&
     (page.error===0||(page.wifi==='local_connected'&&page.error===20));
  }

 get transaction(){return this.#transaction;}
 async profiles(){
  const rows=[];let cursor=0,total=null;
  do{
   const data=result(await this.#connection.command('list',{cursor}));
   if(!revision(data.revision)||!Array.isArray(data.profiles)||data.profiles.length>2||!Number.isInteger(data.count)||data.count>8||data.count<0||
      !Number.isInteger(data.next)||data.next<0||data.next>8||(data.next&&data.next<=cursor))this.#protocol('Invalid saved-network page.');
   if(total!==null&&total!==data.count)this.#protocol('Saved-network count changed.');total=data.count;
   if(data.next&&data.next!==cursor+data.profiles.length)this.#protocol('Invalid saved-network cursor.');
   if(cursor&&data.revision!==this.#revision)this.#protocol('Settings changed while reading. Reconnect.');
   this.#revision=data.revision;
   for(const row of data.profiles){
    this.#network({...row,supported:true,rssi:0});
    if(!Number.isInteger(row.id)||row.id<1||row.id>0xffffffff||typeof row.has_secret!=='boolean'||
      typeof row.enabled!=='boolean'||!Number.isInteger(row.priority)||row.priority<0||row.priority>99||
      Object.keys(row).some(k=>!['id','ssid','open','has_secret','enabled','priority'].includes(k))||
      rows.some(r=>r.id===row.id||(r.ssid===row.ssid&&r.open===row.open)))this.#protocol('Invalid saved network.');
    rows.push(row);
   }
   if(rows.length>8)this.#protocol('Too many saved networks.');cursor=data.next;
  }while(cursor);
  if(rows.length!==total)this.#protocol('Saved-network list is incomplete.');return rows;
 }
 async scan({cancelled=()=>false}={}){
  result(await this.#connection.command('scan'));
  const deadline=Date.now()+15000;
  for(;;){
   if(cancelled())this.#protocol('Wi-Fi search was cancelled.');
   const first=result(await this.#connection.command('scan_page',{cursor:0}));
   if(first.wifi!=='scanning'){
     if(!this.#scanReady(first))throw new WifiInputError(15);
    const rows=[];let page=first,cursor=0;
    for(;;){
     if(!Array.isArray(page.networks)||page.networks.length>2||!Number.isInteger(page.next)||page.next<0||page.next>20||
       (page.next&&page.next<=cursor))this.#protocol('Invalid network page.');
     if(page.next&&page.next!==cursor+page.networks.length)this.#protocol('Invalid network cursor.');
     rows.push(...page.networks.map(row=>this.#network(row)));if(rows.length>20)this.#protocol('Too many network results.');
     if(!page.next)return rows;cursor=page.next;page=result(await this.#connection.command('scan_page',{cursor}));
      if(!this.#scanReady(page))this.#protocol('Network search changed. Search again.');
    }
   }
   if(Date.now()>=deadline)throw new WifiInputError(15);
   await new Promise(resolve=>setTimeout(resolve,350));
  }
 }
 async stage({ssid,password='',open=false,profile=0,keep=false,priority=0,enabled=true}){
  if(typeof ssid!=='string'||!byteLength(ssid)||byteLength(ssid)>32||decode.decode(encode.encode(ssid))!==ssid||/[\x00-\x1f\x7f]/.test(ssid)||
    typeof password!=='string'||decode.decode(encode.encode(password))!==password||typeof open!=='boolean'||typeof keep!=='boolean'||
    typeof enabled!=='boolean'||!Number.isInteger(priority)||priority<0||priority>99||!Number.isInteger(profile)||profile<0||profile>0xffffffff||
    ((open||keep)?password!=='':byteLength(password)<8||byteLength(password)>63||/[\x00-\x1f\x7f]/.test(password)))throw new WifiInputError(6);
  if(!this.#revision)await this.profiles();
  if(!this.#transaction){const begun=result(await this.#connection.command('begin',{revision:this.#revision}));
   if(!revision(begun.transaction)||begun.edit!==0)this.#protocol('Invalid settings transaction.');
   this.#transaction=begun.transaction;this.#edit=0;
  }
  const data=result(await this.#connection.command('stage',{transaction:this.#transaction,profile,ssid,password,open,keep,priority,enabled}));
  password='';
  if(!Number.isInteger(data.profile)||data.profile<1||data.profile>0xffffffff||!Number.isInteger(data.edit)||data.edit<1||data.edit>0xffffffff)this.#protocol('Invalid candidate acknowledgment.');
  this.#profile=data.profile;this.#edit=data.edit;return data;
 }
 async removeProfiles({profiles,keep}){
  if(!Array.isArray(profiles)||profiles.length<1||profiles.length>7||new Set(profiles).size!==profiles.length||
    [...profiles,keep].some(id=>!Number.isInteger(id)||id<1||id>0xffffffff)||profiles.includes(keep))throw new WifiInputError(3);
  if(this.#transaction)this.#protocol('Finish or cancel the current Wi-Fi change first.');
  if(this.#revision===null)this.#protocol('Read saved networks before removing them.');
  const begun=result(await this.#connection.command('begin',{revision:this.#revision}));
  if(!revision(begun.transaction)||begun.edit!==0)this.#protocol('Invalid settings transaction.');
  this.#transaction=begun.transaction;this.#edit=0;this.#profile=keep;
  for(const profile of profiles){
   const ack=result(await this.#connection.command('forget',{transaction:this.#transaction,profile,confirm:false}));
   if(Object.keys(ack).sort().join(',')!=='ok')this.#protocol('Invalid removal acknowledgment.');
   ++this.#edit;
  }
  const state=await this.status();
  if(state.transaction!==this.#transaction||state.revision!==this.#revision||state.edit!==this.#edit||state.phase!==1)this.#protocol('Removal changed while being reviewed. Reconnect.');
  return state;
 }
 async check({onStatus=()=>{},cancelled=()=>false}={}){
  if(!this.#transaction||!this.#profile)this.#protocol('Choose and stage a network first.');
  result(await this.#connection.command('check',{transaction:this.#transaction,profile:this.#profile}));
  const deadline=Date.now()+35000;
  for(;;){
   if(cancelled())this.#protocol('Connection check was cancelled.');
   const state=await this.status();onStatus(state);
   if(state.transaction!==this.#transaction||state.edit!==this.#edit)this.#protocol('The setup session ended or settings changed.');
   if(state.phase===3&&state.check_error===20&&state.wifi==='local_connected'&&state.wifi_profile===this.#profile)return state;
   if(state.wifi==='failed'||state.wifi==='fault')throw new WifiInputError(state.wifi_error||15);
   if(state.phase===4)throw new WifiInputError(state.check_error);
   if(Date.now()>=deadline)throw new WifiInputError(15);
   await new Promise(resolve=>setTimeout(resolve,500));
  }
 }
 async status(){
  const data=result(await this.#connection.command('status'));
  if(!revision(data.revision)||typeof data.durable!=='boolean'||typeof data.store_fault!=='boolean'||
    data.internet_verified!==false||data.cloud_available!==false||data.backups_paused!==true||
    typeof data.wifi!=='string'||!['idle','scanning','cancelling','connecting','local_connected','failed','fault'].includes(data.wifi)||
    !Number.isInteger(data.wifi_profile)||data.wifi_profile<0||data.wifi_profile>0xffffffff||
    !Number.isInteger(data.selected_profile)||data.selected_profile<0||data.selected_profile>0xffffffff||
    !(data.applied_revision==='0'||revision(data.applied_revision))||
    Object.keys(data).some(k=>!['ok','revision','durable','store_fault','wifi','applied_revision','wifi_error','wifi_profile','selected_profile','internet_verified','cloud_available','backups_paused','transaction','edit','phase','check_error'].includes(k)))this.#protocol('Invalid connection status.');
  if(Object.hasOwn(data,'transaction')&&(!revision(data.transaction)||!Number.isInteger(data.edit)||data.edit<0||data.edit>0xffffffff||
    !Number.isInteger(data.phase)||data.phase<1||data.phase>4||!Number.isInteger(data.check_error)||data.check_error<0||data.check_error>20))this.#protocol('Invalid candidate status.');
  if(data.store_fault)throw new WifiInputError(6);return data;
 }
 async backupStatus(){
  const data=result(await this.#connection.command('backup_status'));
  const keys=['ok','cloud_configured','last_backup_unix','verified','pending','failed','backup_error','backups_paused'];
  if(keys.some(k=>!Object.hasOwn(data,k))||Object.keys(data).some(k=>!keys.includes(k))||
     data.backups_paused!==true||typeof data.cloud_configured!=='boolean'||
     !Number.isSafeInteger(data.last_backup_unix)||data.last_backup_unix<0||data.last_backup_unix>253402300799||
     ['verified','pending','failed'].some(k=>!Number.isSafeInteger(data[k])||data[k]<0||data[k]>0xffffffff)||
     !Number.isSafeInteger(data.backup_error)||data.backup_error<-0x80000000||data.backup_error>0x7fffffff)
    this.#protocol('Invalid backup status.');
  return data;
 }
 async save(){
  if(!this.#transaction)this.#protocol('No settings are ready to save.');
  const data=result(await this.#connection.command('commit',{transaction:this.#transaction,revision:this.#revision,edit:this.#edit,accept_local:true}));
  if(data.durable!==true||!revision(data.revision)||BigInt(data.revision)!==BigInt(this.#revision)+1n||data.internet_verified!==false)
    this.#protocol('The device did not confirm durable settings. Reconnect to check.');
  this.#revision=data.revision;this.#transaction=null;this.#profile=0;return data;
 }
 async abort(){
  const transaction=this.#transaction;this.#transaction=null;this.#profile=0;
  if(transaction){const answer=await this.#connection.command('abort',{transaction});
   if(answer?.ok===false&&[4,5].includes(answer.error))return;result(answer);
  }
 }
}

// This is a client for the existing authenticated Security2 mailbox, not a
// separate transport. Only presence/revision facts may come back from cloud
// setup. A saved record is deliberately never described as tested access.
export class CloudInputError extends Error {
 constructor(code,{uncertain=false}={}){super('Cloud settings were not accepted.');this.code=code;this.uncertain=uncertain;}
}
function cloudFields(value,keys){
 return !!value&&typeof value==='object'&&!Array.isArray(value)&&keys.every(key=>Object.hasOwn(value,key))&&Object.keys(value).every(key=>keys.includes(key));
}
function cloudResult(value){
 if(value?.ok===true)return value;
 if(!cloudFields(value,Object.hasOwn(value??{},'store_fault')?['ok','error','store_fault']:['ok','error'])||value.ok!==false||
    ![1,2,3,4,5,6,10,11,12,13,15].includes(value.error)||
    (Object.hasOwn(value,'store_fault')&&(value.error!==15||value.store_fault!==true)))throw new ProtocolError('Invalid cloud settings response.');
 throw new CloudInputError(value.error,{uncertain:value.store_fault===true});
}
export function validateCloudFields(fields){
 if(!fields||typeof fields!=='object'||Array.isArray(fields)||!Object.keys(fields).length||
    Object.keys(fields).some(field=>!['username','app_password','folder'].includes(field)))throw new CloudInputError(6);
 for(const [field,value] of Object.entries(fields)){
  if(typeof value!=='string'||!value.length||decode.decode(encode.encode(value))!==value||/[\x00-\x1f\x7f]/.test(value))throw new CloudInputError(6);
  if(field==='folder'){
   if(value.length>96||!value.split('/').every(segment=>/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(segment)))throw new CloudInputError(6);
  }else if(byteLength(value)>128||(field==='username'&&value.includes(':')))throw new CloudInputError(6);
 }
 return fields;
}
export class CloudSetupClient {
 #connection;#revision=null;#transaction=null;#edit=0;
 constructor(connection){this.#connection=connection;}
 #invalid(){this.#connection.disconnect?.();throw new ProtocolError('Invalid cloud settings state. Reconnect to read the saved revision.');}
 #result(value){try{return cloudResult(value);}catch(error){if(error instanceof ProtocolError)this.#invalid();throw error;}}
 #presence(value){return ['has_username','has_password','has_folder'].every(key=>typeof value[key]==='boolean');}
 async status(){
  const value=this.#result(await this.#connection.command('cloud_status'));
  if(!cloudFields(value,['ok','revision','configured','provider','has_username','has_password','has_folder','editing','edit','store_fault'])||
     !revision(value.revision)||typeof value.configured!=='boolean'||!['koofr','other','none'].includes(value.provider)||
     !this.#presence(value)||typeof value.editing!=='boolean'||!Number.isInteger(value.edit)||value.edit<0||value.edit>0xffffffff||
     typeof value.store_fault!=='boolean'||(value.provider!=='koofr'&&(value.has_username||value.has_password||value.has_folder)))this.#invalid();
  if(value.store_fault)throw new CloudInputError(15,{uncertain:true});
  this.#revision=value.revision;return value;
 }
 async stage(fields){
  validateCloudFields(fields);
  if(this.#transaction)throw new CloudInputError(2);
  if(!this.#revision)await this.status();
  const begun=this.#result(await this.#connection.command('cloud_begin',{revision:this.#revision}));
  if(!cloudFields(begun,['ok','revision','transaction','edit','has_username','has_password','has_folder'])||
     begun.revision!==this.#revision||!revision(begun.transaction)||begun.edit!==0||!this.#presence(begun))this.#invalid();
  this.#transaction=begun.transaction;this.#edit=0;
  let last=begun;
  for(const field of ['username','app_password','folder'])if(Object.hasOwn(fields,field)){
   const value=this.#result(await this.#connection.command('cloud_stage',{transaction:this.#transaction,edit:this.#edit,field,value:fields[field]}));
   if(!cloudFields(value,['ok','transaction','edit','has_username','has_password','has_folder'])||
      value.transaction!==this.#transaction||value.edit!==this.#edit+1||!this.#presence(value))this.#invalid();
   ++this.#edit;last=value;
  }
  if(!last.has_username||!last.has_password||!last.has_folder)throw new CloudInputError(11);
  return last;
 }
 async save({acceptUnverified=false}={}){
  if(!acceptUnverified)throw new CloudInputError(10);
  if(!this.#transaction)throw new CloudInputError(4);
  const value=this.#result(await this.#connection.command('cloud_commit',{transaction:this.#transaction,revision:this.#revision,edit:this.#edit,accept_unverified:true}));
  if(!cloudFields(value,['ok','durable','revision','access_verified'])||value.durable!==true||
     !revision(value.revision)||BigInt(value.revision)!==BigInt(this.#revision)+1n||value.access_verified!==false)this.#invalid();
  this.#revision=value.revision;this.#transaction=null;this.#edit=0;return value;
 }
 async abort(){
  const transaction=this.#transaction;this.#transaction=null;this.#edit=0;if(!transaction)return;
  const value=await this.#connection.command('cloud_abort',{transaction});
  if(value?.ok===false&&[4,5].includes(value.error)&&cloudFields(value,['ok','error']))return;
  const answer=this.#result(value);if(!cloudFields(answer,['ok']))this.#invalid();
 }
}

export class OwnerLoginInputError extends Error {
 constructor(code,{uncertain=false}={}){super('The login change was not confirmed.');this.code=code;this.uncertain=uncertain;}
}
const loginSuccessFields=['ok','error','transaction','revision','edit','received','device','durable','reconnect_required','store_fault'];
export class OwnerLoginClient {
 #connection;#transaction=null;#revision=null;#device=null;#edit=0;#received=0;#commitAttempted=false;#recordFactory;
 constructor(connection,{recordFactory=createOwnerRecord}={}){this.#connection=connection;this.#recordFactory=recordFactory;}
 #invalid(){this.#connection.disconnect?.();throw new ProtocolError('The login response was invalid. Reconnect before trying again.');}
 #answer(value){
  if(value?.ok===false){
   if(!cloudFields(value,['ok','error','store_fault'])||![1,2,3,4,5,6,10,12,13,15].includes(value.error)||typeof value.store_fault!=='boolean'||(value.store_fault&&value.error!==15))this.#invalid();
   throw new OwnerLoginInputError(value.error,{uncertain:value.store_fault});
  }
  if(!cloudFields(value,loginSuccessFields)||value.ok!==true||value.error!==0||!revision(value.transaction)||!revision(value.revision)||
     !Number.isInteger(value.edit)||value.edit<0||value.edit>0xffffffff||!Number.isInteger(value.received)||value.received<0||value.received>432||
     typeof value.device!=='string'||!/^[0-9a-f]{12}$/.test(value.device)||typeof value.durable!=='boolean'||typeof value.reconnect_required!=='boolean'||value.store_fault!==false)this.#invalid();
  return value;
 }
 #same(value){return value.transaction===this.#transaction&&value.revision===this.#revision&&value.device===this.#device;}
 async stage({username,password,cancelled=()=>false}={}){
  validateOwnerLogin(username,password,{newPassword:true});
  if(this.#commitAttempted)throw new ProtocolError('A login save was attempted. Reconnect before sending more login commands.');
  if(this.#transaction)throw new OwnerLoginInputError(2);
  const alive=()=>{if(cancelled())throw new ProtocolError('Login preparation ended. Reconnect before trying again.');};
  alive();await this.#connection.reserveOwnerLogin?.();alive();const begun=this.#answer(await this.#connection.command('login_begin'));
  if(begun.edit!==0||begun.received!==0||begun.durable||begun.reconnect_required)this.#invalid();
  this.#transaction=begun.transaction;this.#revision=begun.revision;this.#device=begun.device;this.#edit=0;this.#received=0;
  let record=null;
  try{
   alive();record=await this.#recordFactory({device:this.#device,username,password,cancelled});password='';username='';alive();
   if(!(record instanceof Uint8Array)||record.length!==432)this.#invalid();
   for(let offset=0;offset<432;offset+=128){
    alive();const chunk=record.subarray(offset,Math.min(offset+128,432));
    const data=Array.from(chunk,value=>value.toString(16).padStart(2,'0')).join('');
    const answer=this.#answer(await this.#connection.command('login_stage',{transaction:this.#transaction,edit:this.#edit,offset,data}));alive();
    if(!this.#same(answer)||answer.edit!==this.#edit+1||answer.received!==offset+chunk.length||answer.durable||answer.reconnect_required)this.#invalid();
    this.#edit=answer.edit;this.#received=answer.received;
   }
   return {staged:true,received:this.#received};
  }finally{record?.fill(0);password='';username='';}
 }
 async save({accept=false}={}){
  if(this.#commitAttempted)throw new ProtocolError('A login save was attempted. Reconnect before sending more login commands.');
  if(!accept)throw new OwnerLoginInputError(10);
  if(!this.#transaction||this.#received!==432)throw new OwnerLoginInputError(4);
  this.#commitAttempted=true;
  const answer=this.#answer(await this.#connection.command('login_commit',{transaction:this.#transaction,revision:this.#revision,edit:this.#edit,accept:true}));
  if(answer.transaction!==this.#transaction||answer.device!==this.#device||answer.revision===this.#revision||answer.edit!==this.#edit||answer.received!==432||!answer.durable||!answer.reconnect_required)this.#invalid();
  this.#transaction=null;return {durable:true,reconnect_required:true};
 }
 async abort(){
  if(this.#commitAttempted)throw new ProtocolError('A login save was attempted. Reconnect before sending more login commands.');
  if(!this.#transaction)return;
  const transaction=this.#transaction;
  const answer=this.#answer(await this.#connection.command('login_abort',{transaction}));
  if(!this.#same(answer)||answer.edit!==0||answer.received!==0||answer.durable||answer.reconnect_required)this.#invalid();
  this.#transaction=null;this.#received=0;this.#edit=0;
 }
}
