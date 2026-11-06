// Explicit, one-shot access on this PC. No credential is kept as object state,
// inserted into the DOM, published, logged or written to browser storage.
const failure=()=>Object.assign(new Error('Saved setup access could not complete.'),{code:'setup_access_unavailable'});
const exact=(value,keys)=>value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===keys.length&&Object.keys(value).every(k=>keys.includes(k));
export function savedAccessAvailable(value){return !!(exact(value,['available'])&&value.available===true);}
export class DirectSetupConnector{
 #bridge;#bluetooth;#prepare;#connect;#cancel;#lock;#epoch=0;#running=false;#clear=()=>{};
 constructor({bridge,bluetooth,prepare,connect,cancel,setPending=()=>{}}){this.#bridge=bridge;this.#bluetooth=bluetooth;this.#prepare=prepare;this.#connect=connect;this.#cancel=cancel;this.#lock=setPending;}
 get running(){return this.#running;}
 async start(port){
  if(this.#running||typeof port!=='string'||!port||port.length>256)throw failure();
  this.#running=true;const epoch=++this.#epoch;let access=null,prepared=false,initialized=false;
  const current=()=>{if(epoch!==this.#epoch)throw failure();};
  const clear=()=>{if(access&&typeof access==='object'){access.key='';access.device_id='';}access=null;};this.#clear=clear;
  try{
   this.#lock(true);
   if(this.#prepare()!==true)throw failure();prepared=true;
   await this.#bluetooth.drain();current();
   access=await this.#bridge.request('usb_setup_access',{port});current();
   if(!exact(access,['key','device_id'])||typeof access.key!=='string'||!/^[A-Za-z0-9_-]{32}$/.test(access.key)||typeof access.device_id!=='string'||!/^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(access.device_id))throw failure();
   const opened=await this.#bridge.request('usb_open_setup',{port});current();
   if(opened?.accepted!==true)throw failure();
   const auth={ownerKey:access.key,bluetooth:this.#bluetooth.forDevice(access.device_id),expectedDevice:access.device_id.replaceAll(':','').toLowerCase()};
   clear();let pending;
   try{pending=this.#connect(auth);}finally{auth.ownerKey='';auth.bluetooth=null;auth.expectedDevice=null;}
   const ready=await pending;current();if(ready!==true)throw failure();initialized=true;return true;
  }catch{if(prepared)this.#cancel();throw failure();}
  finally{
   clear();
   // A late cancelled USB result may have armed the native one-shot target pin.
   // Drain once after that result settles; do not retry an uncertain cleanup.
   if(prepared&&!initialized)try{await this.#bluetooth.drain();}catch{}
   this.#running=false;this.#lock(false);if(epoch===this.#epoch)this.#clear=()=>{};
  }
 }
 cancel(){++this.#epoch;this.#clear();this.#clear=()=>{};this.#cancel();}
}
