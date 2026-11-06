import {test} from 'node:test';
import assert from 'node:assert/strict';
import {ManagementConnection,SESSION,INFO,VERSION,validateInfo,validateMetadata,connectionError} from '../connection.mjs';
const encoder=new TextEncoder();
const metadata={prov:{ver:'v1.1',sec_ver:2,sec_patch_ver:1,cap:[]},csp:{milestone:1,synthetic:true}};
const info={v:1,id:1,ok:true,model:'LILYGO T-Dongle S3',device_id:'CSP-M1-123456',firmware:'prototype',wifi_connected:true,backups_paused:true,synthetic:true};
const value=o=>new DataView(encoder.encode(JSON.stringify(o)).buffer);
function fixture({version=metadata,result=info,hang=false,delayConnect=false}={}) {
 let destroyed=0,disconnected=0,request,resolveConnect,versionCache=new Uint8Array(),versionWrites=0,versionReads=0;const listeners=new Map();
 const chars={
  [VERSION]:{async writeValueWithResponse(data){assert.equal(new TextDecoder().decode(data),'---');versionWrites++;if(data.byteLength)versionCache=encoder.encode(JSON.stringify(version));},async readValue(){versionReads++;return new DataView(versionCache.buffer,versionCache.byteOffset,versionCache.byteLength);}},
  [SESSION]:{async writeValueWithResponse(){},async readValue(){return new DataView(Uint8Array.of(1).buffer);}},
  [INFO]:{async writeValueWithResponse(data){request=JSON.parse(new TextDecoder().decode(data));},async readValue(){return value({...result,id:request.id});}}
 };
 const server={async getPrimaryService(){if(hang)return new Promise(()=>{});return {async getCharacteristic(id){return chars[id];}};}};
 const device={addEventListener:(k,fn)=>listeners.set(k,fn),removeEventListener:k=>listeners.delete(k),gatt:{connected:false,async connect(){if(delayConnect)return new Promise(resolve=>{resolveConnect=()=>{this.connected=true;resolve(server);};});this.connected=true;return server;},disconnect(){this.connected=false;versionCache=new Uint8Array();disconnected++;}}};
 const bluetooth={async requestDevice(options){assert.equal(options.filters[0].namePrefix,'CSP-M1-');return device;}};
 const securityFactory=()=>({async start(){return Uint8Array.of(1);},async challenge(){return Uint8Array.of(2);},finish(){},async encrypt(b){return b;},async decrypt(b){return b;},destroy(){destroyed++;}});
 return {bluetooth,securityFactory,listeners,resolveConnect:()=>resolveConnect(),get connected(){return device.gatt.connected;},get destroyed(){return destroyed;},get disconnected(){return disconnected;},get versionWrites(){return versionWrites;},get versionReads(){return versionReads;},get versionCacheLength(){return versionCache.length;}};
}
test('Web Bluetooth is unavailable outside secure context; no chooser request',async()=>{
 let calls=0;const c=new ManagementConnection({secureContext:false,bluetooth:{requestDevice(){calls++;}}});await assert.rejects(c.connect(),/HTTPS/);assert.equal(calls,0);
});
test('strict metadata rejects insecure or wrong prototype devices',()=>{
 assert.equal(validateMetadata(metadata),metadata);
 for(const m of [{}, {...metadata,prov:{sec_ver:1,sec_patch_ver:1}},{...metadata,prov:{sec_ver:2,sec_patch_ver:0}},{...metadata,csp:{milestone:1,synthetic:false}}])assert.throws(()=>validateMetadata(m),/reviewed/);
});
test('info response binds request ID and rejects unexpected or sensitive fields',()=>{
 assert.equal(validateInfo(info,1),info);
 for(const x of [{...info,id:2},{...info,password:'not-allowed'},{...info,ssid:'not-allowed'},{...info,firmware:'line\nbreak'},{...info,backups_paused:false},{...info,synthetic:false}])assert.throws(()=>validateInfo(x,1),/Invalid/);
});
test('connect authenticates before info, refreshes with increasing IDs and disconnects',async()=>{
 const f=fixture(),states=[],c=new ManagementConnection({...f,secureContext:true,onState:s=>states.push(s.state)});
 assert.deepEqual(await c.connect(),info);assert.deepEqual(states,['choosing','connecting','authenticating','connected']);assert.equal((await c.refresh()).id,2);c.disconnect();assert.equal(f.destroyed,1);assert.equal(f.disconnected,1);
});
test('insecure metadata is closed before synthetic ownership exchange',async()=>{
 const f=fixture({version:{...metadata,prov:{sec_ver:0,sec_patch_ver:1}}}),c=new ManagementConnection({...f,secureContext:true});await assert.rejects(c.connect(),/reviewed/);assert.equal(f.destroyed,0);assert.equal(f.disconnected,1);
});
test('operation timeout disconnects and reports an actionable failure',async()=>{
 const f=fixture({hang:true}),states=[],c=new ManagementConnection({...f,secureContext:true,timeoutMs:15,onState:s=>states.push(s)});await assert.rejects(c.connect(),/timed out/);assert.equal(f.disconnected,1);assert.equal(states.at(-1).state,'error');assert.match(states.at(-1).message,/setup window/);
});
test('remote disconnect destroys session; same client can reconnect',async()=>{
 const f=fixture(),c=new ManagementConnection({...f,secureContext:true});assert.equal(f.versionCacheLength,0);await c.connect();assert(f.versionCacheLength>0);f.listeners.get('gattserverdisconnected')();assert.equal(f.destroyed,1);assert.equal(f.versionCacheLength,0);await c.connect();assert.equal(f.versionWrites,2);assert.equal(f.versionReads,2);c.disconnect();assert.equal(f.destroyed,2);assert.equal(f.versionCacheLength,0);
});
test('refresh without authentication explicitly reports that a new connection is required',async()=>{
 const states=[],c=new ManagementConnection({onState:s=>states.push(s)});await assert.rejects(c.refresh(),/No authenticated session/);assert.equal(states.at(-1).state,'disconnected');assert.match(states.at(-1).message,/Find Cloud Stick/);
});
test('session timeout disconnects without a reset or mutation command',async()=>{
 const f=fixture(),states=[],c=new ManagementConnection({...f,secureContext:true,sessionTimeoutMs:15,onState:s=>states.push(s)});await c.connect();await new Promise(r=>setTimeout(r,25));assert.equal(f.destroyed,1);assert.equal(states.at(-1).state,'disconnected');
});
test('bad response closes connection instead of displaying untrusted fields',async()=>{
 const f=fixture({result:{...info,secret:'no'}}),c=new ManagementConnection({...f,secureContext:true});await assert.rejects(c.connect(),/unexpected/);assert.equal(f.destroyed,1);assert.equal(f.disconnected,1);
});
test('permission/chooser errors do not echo arbitrary platform exception text',()=>{
 assert.match(connectionError({name:'NotFoundError',message:'private platform data'}),/No device selected/);assert(!connectionError(new Error('private data')).includes('private data'));assert.match(connectionError({name:'SecurityError'}),/HTTPS/);
});
test('late GATT completion after timeout is closed and cannot revive the session',async()=>{
 const f=fixture({delayConnect:true}),states=[],c=new ManagementConnection({...f,secureContext:true,timeoutMs:15,onState:s=>states.push(s)});
 await assert.rejects(c.connect(),/timed out/);assert.equal(f.connected,false);
 await assert.rejects(c.connect(),/still closing/);f.resolveConnect();await new Promise(r=>setTimeout(r,0));assert.equal(f.connected,false);assert.equal(f.destroyed,0);assert(!states.some(s=>s.state==='authenticated'||s.state==='connected'));
});
test('manual disconnect closes a GATT connection that completes later',async()=>{
 const f=fixture({delayConnect:true}),states=[],c=new ManagementConnection({...f,secureContext:true,onState:s=>states.push(s)});
 const pending=c.connect();await new Promise(r=>setTimeout(r,0));c.disconnect();f.resolveConnect();await assert.rejects(pending,/closed/);assert.equal(f.connected,false);assert.equal(f.destroyed,0);assert.equal(states.at(-1).state,'disconnected');
});
