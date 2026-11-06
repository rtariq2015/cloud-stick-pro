// Synthetic GATT transport for the ACTUAL ManagementConnection dispatcher.
// Transparent test security is deliberately not a cryptographic/physical test.
import assert from 'node:assert/strict';
import {ManagementConnection,WifiSetupClient,PublicWifiClient,SERVICE,VERSION,SESSION,INFO,WIFI} from '../../phone/wifi-setup/connection.mjs';
import {ProtocolError} from '../../phone/wifi-setup/security2.mjs';
export {ManagementConnection,WifiSetupClient,PublicWifiClient};
const encode=new TextEncoder(),decode=new TextDecoder();
export const VENUE='PRIVATE_SYNTHETIC_VENUE';
export const prepared={ok:true,phase:'prepared',grant:'42',ap_ssid:'CSP-Guest-a1b2c3',ap_password:'admin123',profile:1,revision:'6',window_seconds:300,handoff_ms:30000,next:'handoff_then_disconnect'};
export const handed={ok:true,phase:'disconnect_to_continue',remaining_ms:9000,next:'read_result_then_disconnect',portal_error:0,attempt_dav_read_verified:false};
export const saved={ok:true,revision:'6',durable:true,store_fault:false,wifi:'local_connected',wifi_error:20,wifi_profile:1,selected_profile:1,applied_revision:'6',internet_verified:false,cloud_available:false,backups_paused:true};
export function fixture(options={}){
 let metadata={prov:{sec_ver:2,sec_patch_ver:1},csp:{milestone:2,synthetic:false,owner:'card',wifi:true,cloud:false,public_wifi:true,public_wifi_protocol:1}};
 if(options.metadata)metadata=options.metadata(metadata);
 let step=0,authenticated=false,job=0,pendingData=null,waiting=null,readRelease;
 const requests=[],events=[],states=[],listeners=new Map();
 const reply=(op,request)=>{
  if(options.reply){const data=options.reply(op,request);if(data!==undefined)return data;}
  switch(op){
   case 'portal_begin':return {...prepared};
   case 'portal_handoff':return {...handed};
   case 'portal_cancel':return {ok:true,phase:'complete',portal_error:6,remaining_ms:0,attempt_dav_read_verified:false};
   case 'list':return {ok:true,revision:'6',count:1,profiles:[{id:1,ssid:VENUE,open:true,has_secret:false,priority:0,enabled:true}],next:0};
   case 'status':return {...saved,...options.status};
   case 'abort':return {ok:true};
   case 'scan':return {ok:true};
   case 'scan_page':return {ok:true,has_scan:true,wifi:'local_connected',error:20,networks:[{ssid:VENUE,open:true,supported:true,rssi:-42}],next:0};
   default:throw new Error('Unexpected synthetic operation '+op);
  }
 };
 const characteristics=Object.fromEntries([VERSION,SESSION,INFO,WIFI].map(uuid=>{
  let cached=new Uint8Array(),lastOp='';
  return [uuid,{
   async writeValueWithResponse(payload){
    if(uuid===VERSION){events.push('version:write');assert.equal(decode.decode(payload),'---');cached=encode.encode(JSON.stringify(metadata));return;}
    if(uuid===SESSION){events.push('session:write');assert.equal(payload[0],++step===1?1:3);cached=Uint8Array.of(step===1?2:4);return;}
    assert(authenticated,'Application transport reached before authentication');
    const request=JSON.parse(decode.decode(payload));lastOp=request.op;
    if(uuid===INFO){
     assert.equal(request.op,'info');cached=encode.encode(JSON.stringify({v:1,id:request.id,ok:true,model:'T-Dongle-S3',device_id:'CSP-W2-001122334455',firmware:'SYNTHETIC',wifi_connected:true,backups_paused:true,synthetic:false,...options.info}));return;
    }
    requests.push(request);events.push(request.op+':write');
    let data;
    if(request.op==='result'){assert.equal(request.job,job);data=pendingData;}
    else{++job;data=reply(request.op,request);}
    const pending=options.pendingOp===request.op;
    if(pending)pendingData=data;
    const envelope={v:2,id:request.id,ok:true,job,pending,...(pending?{}:{data})};
    cached=encode.encode(JSON.stringify(options.envelope?options.envelope(envelope,request):envelope));
   },
   async readValue(){
    const result=cached.slice(),op=lastOp;
    if(uuid===WIFI&&options.delayOp===op){waiting=op;await new Promise(resolve=>readRelease=resolve);}
    if(uuid===WIFI)events.push(op+':read-complete');
    return new DataView(result.buffer,result.byteOffset,result.byteLength);
   },
   clear(){cached=new Uint8Array();},
  }];
 }));
 const gatt={connected:false,
  async connect(){this.connected=true;events.push('gatt:connect');return {async getPrimaryService(uuid){assert.equal(uuid,SERVICE);return {async getCharacteristic(id){assert(characteristics[id]);return characteristics[id];}};}};},
  disconnect(){events.push('gatt:disconnect');this.connected=false;step=0;authenticated=false;for(const characteristic of Object.values(characteristics))characteristic.clear();},
 };
 const device={gatt,addEventListener:(name,fn)=>listeners.set(name,fn),removeEventListener:name=>listeners.delete(name)};
 const create=(extra={})=>new ManagementConnection({secureContext:true,timeoutMs:2000,sessionTimeoutMs:10000,
  bluetooth:{async requestDevice(){return device;}},...extra,onState:state=>{states.push(state);extra.onState?.(state);},securityFactory:()=>({
   async start(){return Uint8Array.of(1);},async challenge(bytes){assert.equal(bytes[0],2);return Uint8Array.of(3);},
   finish(bytes){assert.equal(bytes[0],4);if(options.failAuthentication)throw new ProtocolError('Synthetic authentication failure');authenticated=true;},
   async encrypt(bytes){assert(authenticated);return bytes.slice();},async decrypt(bytes){assert(authenticated);return bytes.slice();},destroy(){authenticated=false;},
  })});
 const connection=create();
 return {connection,portal:new PublicWifiClient(connection),flow:new WifiSetupClient(connection),create,requests,events,states,
  get waiting(){return waiting;},get connected(){return gatt.connected;},
  release(){assert(readRelease);readRelease();},remoteDisconnect(){listeners.get('gattserverdisconnected')?.();},
  setMetadata:fn=>metadata=fn(metadata),close:()=>connection.disconnect()};
}
export async function until(predicate){for(let i=0;i<300&&!predicate();++i)await Promise.resolve();assert(predicate(),'Expected asynchronous test checkpoint');}
