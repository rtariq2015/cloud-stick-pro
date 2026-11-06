// Desktop test transport only. Actual Security 2 is supplied by the SDK C host.
const listeners=new Map();
const raw=s=>Uint8Array.from(s.match(/../g)||[],b=>parseInt(b,16));
const hex=b=>Array.from(new Uint8Array(b),v=>v.toString(16).padStart(2,'0')).join('');
const view=b=>new DataView(b.buffer,b.byteOffset,b.byteLength);
let lastSession,lastInfo,lastVersion=new Uint8Array();
const version={async writeValueWithResponse(data){
 if(new TextDecoder().decode(data)!=='---')throw new Error('Expected official nonempty version request');
 lastVersion=new TextEncoder().encode(JSON.stringify({prov:{ver:'v1.1',sec_ver:2,sec_patch_ver:1,cap:[]},csp:{milestone:1,synthetic:true}}));
},async readValue(){return view(lastVersion);}};
const session={
 async writeValueWithResponse(data){const r=await window.sdk('session',hex(data));if(r.error)throw new Error('Synthetic proof rejected');lastSession=raw(r.data);},
 async readValue(){return view(lastSession);}
};
const info={
 async writeValueWithResponse(data){
  let r=await window.sdk('decrypt',hex(data));if(r.error)throw new Error('Tag rejected');
  const request=JSON.parse(new TextDecoder().decode(raw(r.data)));
  const answer={v:1,id:request.id,ok:true,model:'LILYGO T-Dongle S3',device_id:'CSP-M1-DESKTOP',firmware:'0.3.3-dev · M1 synthetic',wifi_connected:true,backups_paused:true,synthetic:true};
  r=await window.sdk('encrypt',hex(new TextEncoder().encode(JSON.stringify(answer))));
  if(r.error)throw new Error('SDK encryption failed');lastInfo=raw(r.data);
 },
 async readValue(){return view(lastInfo);}
};
const service={async getCharacteristic(id){
 if(id.startsWith('021aff53'))return version;
 if(id.startsWith('021aff51'))return session;
 if(id.startsWith('021aff52'))return info;
 throw new Error('Unknown test characteristic');
}};
const server={async getPrimaryService(){return service;}};
const gatt={connected:false,async connect(){this.connected=true;return server;},disconnect(){this.connected=false;lastVersion=new Uint8Array();lastSession=lastInfo=undefined;}};
const device={addEventListener:(key,fn)=>listeners.set(key,fn),removeEventListener:key=>listeners.delete(key),gatt};
Object.defineProperty(navigator,'bluetooth',{value:{async requestDevice(){return device;}},configurable:true});
document.addEventListener('DOMContentLoaded',()=>{
 const banner=document.getElementById('preview-banner');banner.hidden=false;
 banner.textContent='Desktop protocol test · mocked Bluetooth transport · actual Espressif Security 2';
});
