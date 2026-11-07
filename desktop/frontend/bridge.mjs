const SERVICE='021a9004-0382-4aea-bff4-6b3f1c5adfb4';
const UUIDS=new Set([1,2,3,4].map(n=>`021aff5${n}-0382-4aea-bff4-6b3f1c5adfb4`));
const USB_PORT_OPS=new Set(['usb_status','usb_open_setup','usb_setup_access_status','usb_setup_access','usb_forget_setup_access','usb_owner_status','usb_owner_identity','usb_diagnostics','firmware_prepare']);
const OPS=new Set(['discover','connect','read','write','disconnect','usb_ports',...USB_PORT_OPS,'usb_owner_install','review_bundle','firmware_check','firmware_state','firmware_ports','firmware_start']);
const exact=(value,keys)=>value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).every(k=>keys.includes(k));
const error=code=>Object.assign(new Error('The local device operation ended.'),{name:'NetworkError',code});
function requestArgs(op,args){
 if(!OPS.has(op)||!exact(args,op==='firmware_start'?['port','mode']:op==='connect'?['device_id']:op==='read'?['uuid']:op==='write'?['uuid','hex']:op==='usb_owner_install'?['port','record_hex']:USB_PORT_OPS.has(op)?['port']:[]))throw error('invalid_request');
 if(op==='firmware_start'&&(!['new','existing','resume'].includes(args.mode)||typeof args.port!=='string'||!args.port||args.port.length>256))throw error('invalid_request');
 if(op==='connect'&&(typeof args.device_id!=='string'||!args.device_id||args.device_id.length>256))throw error('invalid_device');
 if(['read','write'].includes(op)&&!UUIDS.has(args.uuid))throw error('invalid_characteristic');
 if(op==='write'&&(typeof args.hex!=='string'||!/^(?:[0-9a-f]{2}){1,512}$/.test(args.hex)))throw error('invalid_payload');
 if((USB_PORT_OPS.has(op)||op==='usb_owner_install')&&(typeof args.port!=='string'||!args.port||args.port.length>256))throw error('invalid_port');
 if(op==='usb_owner_install'&&(typeof args.record_hex!=='string'||!/^[0-9a-f]{864}$/.test(args.record_hex)))throw error('invalid_owner_record');
}
export class DesktopBridge {
 #native;#pending=new Map();#next=0;#closed=false;#timeout;#set;#clear;#listeners=new Set();
 constructor(native,{timeoutMs=20000,setTimer=setTimeout,clearTimer=clearTimeout}={}){
  if(typeof native?.request!=='function'||typeof native.response?.connect!=='function'||typeof native.events?.connect!=='function')throw error('bridge_missing');
  // Browser timer methods reject a non-Window receiver. Keep injected timers as
  // lexical calls rather than invoking window.setTimeout with this bridge as this.
  this.#native=native;this.#timeout=timeoutMs;this.#set=(callback,ms)=>setTimer(callback,ms);this.#clear=handle=>clearTimer(handle);
  native.response.connect(this.#response);native.events.connect(this.#event);
 }
 get pendingCount(){return this.#pending.size;}
 onDisconnect(callback){this.#listeners.add(callback);return()=>this.#listeners.delete(callback);}
 request(op,args={}){
  try{requestArgs(op,args);}catch(e){return Promise.reject(e);}
  if(this.#closed||this.#next>=0x7fffffff||this.#pending.size>=8)return Promise.reject(error('bridge_unavailable'));
  const id=++this.#next;
  return new Promise((resolve,reject)=>{
   const timer=this.#set(()=>{this.#pending.delete(id);reject(error('timeout'));},this.#timeout);
   this.#pending.set(id,{op,resolve,reject,timer});
   try{this.#native.request(JSON.stringify({id,op,...args}));}catch{this.#finish(id,false,'bridge_failure');}
  });
 }
 #finish(id,ok,value){const p=this.#pending.get(id);if(!p)return;this.#pending.delete(id);this.#clear(p.timer);ok?p.resolve(value):p.reject(error(value));}
 #response=raw=>{
  if(this.#closed||typeof raw!=='string'||raw.length>32768)return;
  let r;try{r=JSON.parse(raw);}catch{return;}
  if(!exact(r,['id','ok','result','error'])||!Number.isSafeInteger(r.id)||typeof r.ok!=='boolean'||!this.#pending.has(r.id))return;
  if(r.ok){if(!Object.hasOwn(r,'result')||Object.hasOwn(r,'error')){this.#finish(r.id,false,'invalid_response');return;}this.#finish(r.id,true,r.result);}
  else if(exact(r.error,['code'])&&typeof r.error.code==='string'&&/^[a-zA-Z0-9_.-]{1,64}$/.test(r.error.code)&&!Object.hasOwn(r,'result'))this.#finish(r.id,false,r.error.code);
  else this.#finish(r.id,false,'invalid_response');
 };
 #event=raw=>{
  if(this.#closed||typeof raw!=='string'||raw.length>1024)return;
  let e;try{e=JSON.parse(raw);}catch{return;}
  if(!exact(e,['type','device_id'])||e.type!=='disconnected'||(e.device_id!==undefined&&(typeof e.device_id!=='string'||e.device_id.length>256)))return;
  // Outstanding GATT operations belong to the ended session; USB work is independent.
  for(const [id,p]of this.#pending)if(['connect','read','write','discover'].includes(p.op))this.#finish(id,false,'disconnected');
  for(const cb of this.#listeners)cb(e);
 };
 close(){if(this.#closed)return;this.#closed=true;for(const [id]of this.#pending)this.#finish(id,false,'bridge_closed');this.#listeners.clear();this.#native.response.disconnect?.(this.#response);this.#native.events.disconnect?.(this.#event);}
}
function validateDevices(value){
 if(!exact(value,['devices'])||!Array.isArray(value.devices)||value.devices.length>64)throw error('invalid_discovery');
 const ids=new Set();return value.devices.map(d=>{
  if(!exact(d,['id','name'])||typeof d.id!=='string'||!d.id||d.id.length>256||ids.has(d.id)||typeof d.name!=='string'||d.name.length>96||/[\x00-\x1f\x7f]/.test(d.name))throw error('invalid_discovery');
  ids.add(d.id);return {id:d.id,name:d.name};
 });
}
export class NativeBluetooth {
 #bridge;#choose;#epoch=0;#current=null;#choosing=false;#off;#closing=null;
 constructor(bridge,{chooseDevice}={}){if(typeof chooseDevice!=='function')throw error('chooser_missing');this.#bridge=bridge;this.#choose=chooseDevice;this.#off=bridge.onDisconnect(e=>{if(this.#current&&(!e.device_id||e.device_id===this.#current.id))this.#lost(this.#current);});}
 get active(){return this.#choosing||this.#current!==null;}
 forDevice(id){
  if(typeof id!=='string'||!id||id.length>256||/[\x00-\x1f\x7f]/.test(id))throw error('invalid_device');
  return {requestDevice:options=>this.#requestDevice(options,id)};
 }
 requestDevice(options){return this.#requestDevice(options,null);}
 async #requestDevice(options,exactId){
  const nameFilter=options?.filters?.length===1&&options.filters[0].namePrefix==='CSP-W2-'&&options?.optionalServices?.includes(SERVICE);
  const serviceFilter=options?.filters?.some(f=>f.services?.includes(SERVICE));
  if((!nameFilter&&!serviceFilter)||this.#choosing||this.#current)throw error('invalid_selection');
  this.#choosing=true;const epoch=this.#epoch;
  try{
   if(this.#closing){await this.#closing;if(epoch!==this.#epoch)throw error('cancelled');this.#closing=null;}
   const discovered=validateDevices(await this.#bridge.request('discover'));if(epoch!==this.#epoch)throw error('cancelled');
   const rows=nameFilter?discovered.filter(row=>row.name.startsWith('CSP-W2-')):discovered;
   // A trusted USB-bound bootstrap has exactly one allowed native device ID.
   // Never choose the first result or fall back to the interactive chooser.
   const id=exactId??await this.#choose(rows);if(epoch!==this.#epoch)throw error('cancelled');
   const row=rows.find(r=>r.id===id);if(!row)throw Object.assign(new Error('No device selected.'),{name:'NotFoundError'});
   const listeners=new Set(),owner=this;
   const device={id:row.id,name:row.name,addEventListener(type,cb){if(type==='gattserverdisconnected')listeners.add(cb);},removeEventListener(type,cb){if(type==='gattserverdisconnected')listeners.delete(cb);},gatt:{connected:false,async connect(){
    if(owner.#current!==device)throw error('stale_device');await owner.#bridge.request('connect',{device_id:device.id});
    if(owner.#current!==device||epoch!==owner.#epoch)throw error('disconnected');this.connected=true;return this;
   },async getPrimaryService(uuid){
    if(uuid!==SERVICE||owner.#current!==device||!this.connected)throw error('invalid_service');
    return {async getCharacteristic(uuid){
     if(!UUIDS.has(uuid)||owner.#current!==device||!device.gatt.connected)throw error('invalid_characteristic');
     const alive=()=>{if(owner.#current!==device||!device.gatt.connected)throw error('disconnected');};
     return {async writeValueWithResponse(data){alive();const bytes=data instanceof Uint8Array?data:ArrayBuffer.isView(data)?new Uint8Array(data.buffer,data.byteOffset,data.byteLength):data instanceof ArrayBuffer?new Uint8Array(data):null;
      if(!bytes||bytes.length<1||bytes.length>512)throw error('invalid_payload');const hex=Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');await owner.#bridge.request('write',{uuid,hex});alive();},
      async readValue(){alive();const r=await owner.#bridge.request('read',{uuid});alive();if(!exact(r,['hex'])||typeof r.hex!=='string'||!/^(?:[0-9a-f]{2}){0,512}$/.test(r.hex))throw error('invalid_payload');const bytes=Uint8Array.from(r.hex.match(/../g)??[],h=>parseInt(h,16));return new DataView(bytes.buffer);}};
    }};
   },disconnect(){if(owner.#current!==device)return;owner.#queueDisconnect();owner.#lost(device);}},_notify(){for(const cb of listeners)cb({type:'gattserverdisconnected',target:device});}};
   this.#current=device;return device;
  }finally{this.#choosing=false;}
 }
 #lost(device){if(this.#current!==device)return;this.#current=null;++this.#epoch;device.gatt.connected=false;device._notify();}
 #queueDisconnect(){const pending=this.#bridge.request('disconnect');this.#closing=pending;pending.catch(()=>{});return pending;}
 cancel(){++this.#epoch;if(this.#current){const d=this.#current;this.#queueDisconnect();this.#lost(d);}}
 // Reuse an already queued close. Native ownership is not free until it settles.
 drain(){const pending=this.#closing??this.#queueDisconnect();return pending.then(value=>{if(this.#closing===pending)this.#closing=null;return value;});}
 destroy(){this.cancel();this.#off();}
}
export {SERVICE};
