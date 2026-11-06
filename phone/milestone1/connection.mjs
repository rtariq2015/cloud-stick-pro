import {Security2Client,ProtocolError,MAX_PACKET} from './security2.mjs';
export const SERVICE='021a9004-0382-4aea-bff4-6b3f1c5adfb4';
export const SESSION='021aff51-0382-4aea-bff4-6b3f1c5adfb4';
export const INFO='021aff52-0382-4aea-bff4-6b3f1c5adfb4';
export const VERSION='021aff53-0382-4aea-bff4-6b3f1c5adfb4';
const encode=new TextEncoder(),decode=new TextDecoder('utf-8',{fatal:true});
function packet(view){const out=view instanceof Uint8Array?view:new Uint8Array(view.buffer,view.byteOffset,view.byteLength);if(out.length>MAX_PACKET)throw new ProtocolError('Device response is too large');return out.slice();}
export function validateMetadata(value){
 if(!value||value.prov?.sec_ver!==2||value.prov?.sec_patch_ver!==1||value.csp?.milestone!==1||value.csp?.synthetic!==true)throw new ProtocolError('This device does not offer the reviewed Milestone 1 protocol');
 return value;
}
export function validateInfo(value,id){
 const allowed=['v','id','ok','model','device_id','firmware','wifi_connected','backups_paused','synthetic'];
 if(!value||Object.keys(value).some(k=>!allowed.includes(k))||value.v!==1||value.id!==id||value.ok!==true||value.synthetic!==true||value.backups_paused!==true||typeof value.wifi_connected!=='boolean')throw new ProtocolError('Invalid or unexpected device information');
 for(const k of ['model','device_id','firmware'])if(typeof value[k]!=='string'||value[k].length<1||value[k].length>96||/[\x00-\x1f\x7f]/.test(value[k]))throw new ProtocolError('Invalid device identification');
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
 #bluetooth;#secure;#device=null;#security=null;#generation=0;#id=0;#busy=false;#timeout;#timer=null;#onState;#securityFactory;#session=null;#info=null;#pendingConnect=null;
 constructor({bluetooth=globalThis.navigator?.bluetooth,secureContext=globalThis.isSecureContext,onState=()=>{},timeoutMs=15000,sessionTimeoutMs=120000,securityFactory=()=>new Security2Client('wifiprov','abcd1234')}={}){
  this.#bluetooth=bluetooth;this.#secure=secureContext;this.#onState=onState;this.#timeout=timeoutMs;this.sessionTimeoutMs=sessionTimeoutMs;this.#securityFactory=securityFactory;
 }
 #notify(state,message,info){this.#onState({state,message,info});}
 #assert(generation){if(generation!==this.#generation)throw new ProtocolError('Connection was closed');}
 async #bounded(promise,generation){
  let timer;try{const result=await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new ProtocolError('Connection timed out. Reopen the physical setup window and reconnect.')),this.#timeout);})]);this.#assert(generation);return result;}finally{clearTimeout(timer);}
 }
 async #exchange(characteristic,payload,generation){
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
   this.#device=await this.#bounded(this.#bluetooth.requestDevice({filters:[{namePrefix:'CSP-M1-'}],optionalServices:[SERVICE]}),generation);
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
   validateMetadata(JSON.parse(decode.decode(await this.#exchange(version,encode.encode('---'),generation))));
   this.#session=await this.#bounded(service.getCharacteristic(SESSION),generation);this.#info=await this.#bounded(service.getCharacteristic(INFO),generation);
   this.#security=this.#securityFactory();this.#notify('authenticating','Checking the synthetic ownership proof…');
   const first=await this.#bounded(this.#security.start(),generation);
   const challenge=await this.#exchange(this.#session,first,generation);
   const proof=await this.#bounded(this.#security.challenge(challenge),generation);
   this.#security.finish(await this.#exchange(this.#session,proof,generation));
   const result=await this.#readInfo(generation);this.#notify('connected','Authenticated test connection. Device information is read-only.',result);
   this.#timer=setTimeout(()=>{this.disconnect(false);this.#notify('disconnected','The two-minute phone session ended. Reconnect when you need it.');},this.sessionTimeoutMs);
   return result;
  }catch(error){if(generation===this.#generation){this.disconnect(false);this.#notify('error',connectionError(error));}throw error;}
  finally{this.#busy=false;}
 }
 async #readInfo(generation){
  const id=++this.#id,request=encode.encode(JSON.stringify({v:1,id,op:'info'}));
  const encrypted=await this.#bounded(this.#security.encrypt(request),generation);
  const answer=await this.#exchange(this.#info,encrypted,generation);
  const plain=await this.#bounded(this.#security.decrypt(answer),generation);
  return validateInfo(JSON.parse(decode.decode(plain)),id);
 }
 async refresh(){
  if(!this.#security){const error=new ProtocolError('No authenticated session. Tap Find Cloud Stick to connect again.');this.#notify('disconnected',error.message);throw error;}
  if(this.#busy)throw new ProtocolError('A Bluetooth operation is still finishing. Wait a moment, then try again.');
  this.#busy=true;const generation=this.#generation;
  try{const info=await this.#readInfo(generation);this.#notify('connected','Device information refreshed.',info);return info;}
  catch(error){if(generation===this.#generation){this.disconnect(false);this.#notify('error',connectionError(error));}throw error;}finally{this.#busy=false;}
 }
 #disconnected=()=>{this.disconnect(false);this.#notify('disconnected','Bluetooth disconnected. The dongle keeps your files. Reconnect when ready.');};
 disconnect(notify=true){
  ++this.#generation;clearTimeout(this.#timer);this.#timer=null;this.#security?.destroy();this.#security=null;this.#session=this.#info=null;this.#id=0;
  const device=this.#device;this.#device=null;
  if(device){device.removeEventListener('gattserverdisconnected',this.#disconnected);try{device.gatt.disconnect();}catch{}}
  if(notify)this.#notify('disconnected','Disconnected. You can reconnect through a new setup window.');
 }
}
