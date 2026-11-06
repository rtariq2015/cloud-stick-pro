import {createOwnerRecord,validateOwnerLogin} from './shared/security2.mjs';

const exact=(value,keys)=>!!value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===keys.length&&Object.keys(value).every(k=>keys.includes(k));
const uint=(value,max)=>Number.isSafeInteger(value)&&value>=0&&value<=max;
const fail=code=>Object.assign(new Error('First-account setup needs review.'),{code});
export function ownerStatus(value){
 const fields=['owner_state','boot_down','held_ms','blocked','can_enroll','last_blocked','install_step'];
 if(!exact(value,fields)||!uint(value.owner_state,2)||typeof value.boot_down!=='boolean'||!uint(value.held_ms,0xffffffff)||!uint(value.blocked,1023)||typeof value.can_enroll!=='boolean'||!uint(value.last_blocked,1023)||!uint(value.install_step,6))throw fail('invalid_owner_status');
 if(value.can_enroll!==(value.blocked===0))throw fail('invalid_owner_status');
 if(value.can_enroll&&(value.owner_state!==1||!value.boot_down||value.held_ms<10000||value.blocked!==0))throw fail('invalid_owner_status');
 return {...value};
}
export function ownerIdentity(value){if(!exact(value,['device'])||typeof value.device!=='string'||!/^[0-9a-f]{12}$/.test(value.device))throw fail('invalid_owner_identity');return value.device;}
const nativeId=device=>device.match(/../g).join(':').toUpperCase();

// The candidate and chosen password exist only in this private, bounded RAM
// scope. Published state is boolean-only; no record or credential enters UI.
export class FirstOwnerSetup{
 #bridge;#bluetooth;#prepare;#connect;#close;#lock;#emit;#recordFactory;#set;#clearTimer;#timer=null;#epoch=0;#busy=false;#candidate=null;#canEnroll=false;#installed=false;#unknown=false;#confirmed=false;#expired=false;#writing=false;
 constructor({bridge,bluetooth,prepare,connect,cancel,setPending=()=>{},onState=()=>{},recordFactory=createOwnerRecord,setTimer=setTimeout,clearTimer=clearTimeout}){
  this.#bridge=bridge;this.#bluetooth=bluetooth;this.#prepare=prepare;this.#connect=connect;this.#close=cancel;this.#lock=setPending;this.#emit=onState;this.#recordFactory=recordFactory;this.#set=(fn,ms)=>setTimer(fn,ms);this.#clearTimer=id=>clearTimer(id);
 }
 get running(){return this.#busy;}
 state(){return Object.freeze({busy:this.#busy,prepared:!!this.#candidate,canEnroll:this.#canEnroll,installed:this.#installed,unknown:this.#unknown,confirmed:this.#confirmed,expired:this.#expired});}
 #publish(){this.#emit(this.state());}
 #wipe(){if(this.#timer!==null)this.#clearTimer(this.#timer);this.#timer=null;if(this.#candidate){this.#candidate.record?.fill(0);this.#candidate.username='';this.#candidate.password='';this.#candidate.device='';}this.#candidate=null;this.#canEnroll=false;}
 async #run(action){
  if(this.#busy)throw fail('owner_setup_busy');this.#busy=true;const epoch=this.#epoch;this.#lock(true);this.#publish();
  const alive=()=>{if(epoch!==this.#epoch)throw fail('cancelled');};
  try{return await action(alive);}
  catch(error){
   if(this.#writing||error?.code==='owner_install_unknown')this.#unknown=true;
   this.#writing=false;this.#wipe();this.#close();
   throw fail(this.#unknown?'owner_install_unknown':this.#installed?'owner_confirmation_needed':error?.code==='encoding'?'owner_login_encoding':error?.code==='owner_release_required'?'owner_release_required':'owner_setup_failed');
  }finally{
   if(!this.#candidate&&!this.#confirmed)try{await this.#bluetooth.drain();}catch{}
   this.#busy=false;if(!this.#candidate)this.#lock(false);this.#publish();
  }
 }
 async review(port,{username,password,confirmation}){
  if(this.#candidate||this.#installed||this.#unknown||typeof port!=='string'||!port||port.length>256)throw fail('owner_setup_unavailable');
  validateOwnerLogin(username,password,{newPassword:true});if(password!==confirmation)throw fail('owner_confirmation_mismatch');confirmation='';
  const epoch=this.#epoch;
  try{return await this.#run(async alive=>{
   if(this.#prepare()!==true)throw fail('owner_setup_unavailable');
   await this.#bluetooth.drain();alive();
   if(ownerStatus(await this.#bridge.request('usb_owner_status',{port})).owner_state!==1)throw fail('owner_not_missing');alive();
   const device=ownerIdentity(await this.#bridge.request('usb_owner_identity',{port}));alive();
   let record=await this.#recordFactory({device,username,password,cancelled:()=>this.#epoch!==epoch});
   try{alive();if(!(record instanceof Uint8Array)||record.length!==432)throw fail('owner_record_invalid');
    this.#candidate={port,device,username,password,record};record=null;username='';password='';this.#expired=false;
    this.#timer=this.#set(()=>{this.#expired=true;this.cancel();},120000);return true;
   }finally{record?.fill(0);}
  });}finally{username='';password='';}
 }
 async readiness(){
  if(!this.#candidate||this.#installed||this.#unknown)throw fail('owner_setup_unavailable');
  return this.#run(async alive=>{const status=ownerStatus(await this.#bridge.request('usb_owner_status',{port:this.#candidate.port}));alive();if(status.owner_state!==1)throw fail('owner_not_missing');this.#canEnroll=status.can_enroll&&status.boot_down&&status.held_ms>=10000&&status.blocked===0;return this.#canEnroll;});
 }
 async install({accept=false}={}){
  if(accept!==true||!this.#candidate||!this.#canEnroll||this.#installed||this.#unknown)throw fail('owner_setup_unavailable');
  return this.#run(async alive=>{
   const request={port:this.#candidate.port,record_hex:Array.from(this.#candidate.record,b=>b.toString(16).padStart(2,'0')).join('')};this.#writing=true;
   let result;try{result=await this.#bridge.request('usb_owner_install',request);}finally{request.record_hex='';}
   alive();if(!exact(result,['installed'])||result.installed!==true)throw fail('owner_install_unknown');
   this.#writing=false;this.#installed=true;this.#canEnroll=false;this.#candidate.record.fill(0);this.#candidate.record=null;return true;
  });
 }
 async confirm(){
  if(!this.#installed||!this.#candidate||this.#unknown||this.#confirmed)throw fail('owner_setup_unavailable');
  return this.#run(async alive=>{
   await this.#bluetooth.drain();alive();
   const status=ownerStatus(await this.#bridge.request('usb_owner_status',{port:this.#candidate.port}));alive();
   if(status.owner_state!==0)throw fail('owner_confirmation_needed');
   if(status.boot_down)return false; // Let the user release BOOT, then explicitly continue.
   const device=ownerIdentity(await this.#bridge.request('usb_owner_identity',{port:this.#candidate.port}));alive();if(device!==this.#candidate.device)throw fail('owner_device_changed');
   const opened=await this.#bridge.request('usb_open_setup',{port:this.#candidate.port});alive();if(opened?.accepted!==true)throw fail('owner_setup_failed');
   const auth={username:this.#candidate.username,password:this.#candidate.password,expectedDevice:device,bluetooth:this.#bluetooth.forDevice(nativeId(device))};
   this.#wipe();let pending;try{pending=this.#connect(auth);}finally{auth.username='';auth.password='';auth.expectedDevice=null;auth.bluetooth=null;}
   const ready=await pending;alive();if(ready!==true)throw fail('owner_confirmation_needed');this.#confirmed=true;return true;
  });
 }
 cancel(){
  ++this.#epoch;if(this.#writing)this.#unknown=true;this.#writing=false;this.#wipe();this.#close();
  if(!this.#busy){this.#busy=true;this.#lock(true);this.#publish();return this.#bluetooth.drain().catch(()=>{}).finally(()=>{this.#busy=false;this.#lock(false);this.#publish();});}
  this.#publish();
 }
}
