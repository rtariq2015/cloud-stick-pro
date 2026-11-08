// Typed native GATT and isolated public LAN status operations. Security2 and owner authentication remain in the unchanged shared client.
export const SERVICE='021a9004-0382-4aea-bff4-6b3f1c5adfb4';
const UUIDS=new Set(['51','52','53','54'].map(n=>`021aff${n}-0382-4aea-bff4-6b3f1c5adfb4`));
const hex=value=>Array.from(new Uint8Array(value.buffer??value,value.byteOffset??0,value.byteLength??value.length),b=>b.toString(16).padStart(2,'0')).join('');
const network=message=>Object.assign(new Error(message),{name:'NetworkError'});
export class NativeRPC {
 #endpoint;#next=0;#pending=new Map();#events=new Set();#timeout;
 constructor(endpoint=globalThis.CloudStickNative,{timeout=20000}={}) {
  if(!endpoint?.postMessage)throw network('The app connection is unavailable.');
  this.#endpoint=endpoint;this.#timeout=timeout;
  endpoint.onmessage=event=>this.#receive(event.data);
 }
 #receive(raw) {
  let response;try{response=JSON.parse(raw);}catch{return;}
  if(!response||!Number.isSafeInteger(response.id)||typeof response.ok!=='boolean')return;
  const pending=this.#pending.get(response.id);if(!pending)return;
  if(response.ok ? Object.keys(response).sort().join(',')!=='id,ok,result'||!response.result||typeof response.result!=='object'||Array.isArray(response.result)
   : Object.keys(response).sort().join(',')!=='error,id,ok'||typeof response.error!=='string'||!/^[a-z_]{1,64}$/.test(response.error))return;
  clearTimeout(pending.timer);this.#pending.delete(response.id);
  response.ok?pending.resolve(response.result):pending.reject(network(response.error));
 }
 onEvent(listener){this.#events.add(listener);return()=>this.#events.delete(listener);}
 event(event){if(event?.type==='disconnected'&&Object.keys(event).length===1)for(const listener of this.#events)listener(event);}
 request(op,args={}) {
  if(this.#pending.size>=4||this.#next>=0x7fffffff)return Promise.reject(network('A device request is still finishing.'));
  const id=++this.#next;
  return new Promise((resolve,reject)=>{
   const timer=setTimeout(()=>{this.#pending.delete(id);reject(network('The device request timed out.'));
    if(op.startsWith('dashboard_')){if(op!=='dashboard_cancel')this.request('dashboard_cancel').catch(()=>{});}else if(op!=='disconnect')this.request('disconnect').catch(()=>{});},op==='permission'?65000:this.#timeout);
   this.#pending.set(id,{resolve,reject,timer});
   try{this.#endpoint.postMessage(JSON.stringify({id,op,...args}));}catch{clearTimeout(timer);this.#pending.delete(id);reject(network('The app connection is unavailable.'));}
  });
 }
}
export class NativeBluetooth {
 #rpc;#choose;#generation=0;#device=null;#closing=Promise.resolve();#choosing=false;
 constructor(rpc,choose){this.#rpc=rpc;this.#choose=choose;rpc.onEvent(()=>{const device=this.#device;this.#device=null;if(device)device._ended();});}
 async drain(){await this.#closing;}
 close(){++this.#generation;const device=this.#device;this.#device=null;if(device)device._ended();
  this.#closing=this.#closing.catch(()=>{}).then(()=>this.#rpc.request('disconnect'));return this.#closing;}
 async requestDevice(options){
  if(this.#choosing)throw network('A Bluetooth selection is already open.');
  if(options?.filters?.length!==1||options.filters[0].namePrefix!=='CSP-W2-'||options.optionalServices?.length!==1||options.optionalServices[0]!==SERVICE)throw network('Unsupported Bluetooth service.');
  this.#choosing=true;const generation=this.#generation;
  try{
   await this.drain();const response=await this.#rpc.request('discover');
   if(generation!==this.#generation)throw network('Selection cancelled.');
   if(!Array.isArray(response.devices)||response.devices.length>16||response.devices.some(d=>!d||Object.keys(d).sort().join(',')!=='id,name'||typeof d.id!=='string'||!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(d.id)||typeof d.name!=='string'||!/^CSP-W2-[A-Za-z0-9-]{1,32}$/.test(d.name)))throw network('Invalid device inventory.');
   if(!response.devices.length)throw Object.assign(new Error('No nearby Cloud Stick found. Open its setup window first.'),{name:'NotFoundError'});
   const id=await this.#choose(response.devices);
   if(generation!==this.#generation||!response.devices.some(d=>d.id===id))throw Object.assign(new Error('Selection cancelled.'),{name:'NotFoundError'});
   const callbacks=new Set(),rpc=this.#rpc,owner=this;
   let linked=false;
   const characteristic=uuid=>({
    async writeValueWithResponse(value){const encoded=hex(value);if(!linked||!UUIDS.has(uuid)||!/^([0-9a-f]{2}){1,512}$/.test(encoded))throw network('Invalid Bluetooth write.');await rpc.request('write',{uuid,hex:encoded});},
    async readValue(){if(!linked)throw network('Bluetooth is disconnected.');const result=await rpc.request('read',{uuid});if(typeof result.hex!=='string'||!/^([0-9a-f]{2}){0,512}$/.test(result.hex))throw network('Invalid Bluetooth response.');return new DataView(Uint8Array.from(result.hex.match(/../g)??[],h=>parseInt(h,16)).buffer);}
   });
   const device={id,name:response.devices.find(d=>d.id===id).name,
    addEventListener(type,listener){if(type==='gattserverdisconnected')callbacks.add(listener);},
    removeEventListener(type,listener){if(type==='gattserverdisconnected')callbacks.delete(listener);},
    _ended(){linked=false;for(const callback of callbacks)callback();callbacks.clear();},
    gatt:{get connected(){return linked;},
     async connect(){await owner.drain();await rpc.request('connect',{device_id:id});if(generation!==owner.#generation){await owner.close();throw network('Connection cancelled.');}linked=true;return this;},
     async getPrimaryService(uuid){if(!linked||uuid!==SERVICE)throw network('Unsupported Bluetooth service.');return {async getCharacteristic(uuid){if(!UUIDS.has(uuid))throw network('Unsupported Bluetooth characteristic.');return characteristic(uuid);}};},
     disconnect(){void owner.close().catch(()=>{});}
    }};
   this.#device=device;return device;
  }finally{this.#choosing=false;}
 }
}
