// Real ManagementConnection and WifiSetupClient, with simulated GATT and a
// transparent synthetic security fixture. This tests admission/lifecycle wiring;
// Security2 cryptographic interoperability is covered by the separate SDK suite.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {ManagementConnection,WifiSetupClient,SERVICE,VERSION,SESSION,INFO,WIFI} from '../../phone/wifi-setup/connection.mjs';
import {ProtocolError} from '../../phone/wifi-setup/security2.mjs';

const encoder=new TextEncoder(),decoder=new TextDecoder();
const backup={ok:true,cloud_configured:true,last_backup_unix:1793793600,verified:12,pending:2,failed:0,backup_error:0,backups_paused:true};
const wifiStatus={ok:true,revision:'6',durable:true,store_fault:false,wifi:'local_connected',wifi_error:20,wifi_profile:1,selected_profile:1,
 applied_revision:'6',internet_verified:false,cloud_available:false,backups_paused:true};

function fixture({capability=true,infoCapability,failAuthentication=false,pending=false,delayBackupRead=false}={}){
 let advertised=capability,sessionStep=0,authenticated=false,nextJob=0,releaseRead,awaitingRead=false;
 const commands=[],events=new Map(),states=[];
 const characteristics=Object.fromEntries([VERSION,SESSION,INFO,WIFI].map(uuid=>{
  let cached=new Uint8Array();
  return [uuid,{
   async writeValueWithResponse(payload){
    if(uuid===VERSION){
     assert.equal(decoder.decode(payload),'---');
     cached=encoder.encode(JSON.stringify({prov:{sec_ver:2,sec_patch_ver:1},csp:{milestone:2,synthetic:false,owner:'card',wifi:true,cloud:false,
      ...(advertised===undefined?{}:{backup_status:advertised})}}));return;
    }
    if(uuid===SESSION){assert.equal(payload[0],++sessionStep===1?1:3);cached=Uint8Array.of(sessionStep===1?2:4);return;}
    assert(authenticated,'Application characteristic reached before authentication');
    const request=JSON.parse(decoder.decode(payload));
    if(uuid===INFO){
     assert.equal(request.op,'info');cached=encoder.encode(JSON.stringify({v:1,id:request.id,ok:true,model:'T-Dongle-S3',device_id:'CSP-W2-001122334455',firmware:'SYNTHETIC',
      wifi_connected:true,backups_paused:true,synthetic:false,...(infoCapability===undefined?{}:{backup_status:infoCapability})}));return;
    }
    commands.push(request.op);
    const data=request.op==='status'?wifiStatus:backup;
    const waiting=pending&&request.op==='backup_status';
    if(request.op!=='result')++nextJob;
    cached=encoder.encode(JSON.stringify({v:2,id:request.id,ok:true,job:nextJob,pending:waiting,...(waiting?{}:{data})}));
   },
   async readValue(){
    if(uuid===WIFI&&delayBackupRead&&!awaitingRead){awaitingRead=true;await new Promise(resolve=>releaseRead=resolve);}
    return new DataView(cached.buffer,cached.byteOffset,cached.byteLength);
   },
   clear(){cached=new Uint8Array();},
  }];
 }));
 const gatt={connected:false,
  async connect(){this.connected=true;return {async getPrimaryService(uuid){assert.equal(uuid,SERVICE);return {async getCharacteristic(id){assert(characteristics[id]);return characteristics[id];}};}};},
  disconnect(){this.connected=false;sessionStep=0;authenticated=false;for(const characteristic of Object.values(characteristics))characteristic.clear();},
 };
 const device={gatt,addEventListener:(name,handler)=>events.set(name,handler),removeEventListener:name=>events.delete(name)};
 const connection=new ManagementConnection({secureContext:true,timeoutMs:1000,sessionTimeoutMs:10000,
  bluetooth:{async requestDevice(){return device;}},onState:state=>states.push(state),securityFactory:()=>({
   async start(){return Uint8Array.of(1);},async challenge(bytes){assert.equal(bytes[0],2);return Uint8Array.of(3);},
   finish(bytes){assert.equal(bytes[0],4);if(failAuthentication)throw new ProtocolError('Synthetic authentication failure');authenticated=true;},
   async encrypt(bytes){assert(authenticated);return bytes.slice();},async decrypt(bytes){assert(authenticated);return bytes.slice();},
   destroy(){authenticated=false;},
  })});
 return {connection,flow:new WifiSetupClient(connection),commands,states,
  setCapability:value=>advertised=value,releaseRead:()=>releaseRead(),get awaitingRead(){return awaitingRead;},close:()=>connection.disconnect()};
}
async function until(predicate){for(let i=0;i<50&&!predicate();++i)await Promise.resolve();assert(predicate());}

test('real ManagementConnection admits advertised backup_status after authentication',async()=>{
 const f=fixture();try{
  const info=await f.connection.connect();assert.equal(info.backup_status,true);
  const result=await f.flow.backupStatus();assert.equal(result.verified,12);assert.equal(result.backups_paused,true);
  assert.deepEqual(f.commands,['backup_status']);
 }finally{f.close();}
});
test('real ManagementConnection polls the dedicated backup result through its own dispatcher',async()=>{
 const f=fixture({pending:true});try{
  await f.connection.connect();assert.equal((await f.flow.backupStatus()).last_backup_unix,backup.last_backup_unix);
  assert.deepEqual(f.commands,['backup_status','result']);
 }finally{f.close();}
});
for(const capability of [undefined,false])test('old device '+String(capability)+' refuses backup_status before any GATT application write',async()=>{
 const f=fixture();f.setCapability(capability);try{
  const info=await f.connection.connect();assert.notEqual(info.backup_status,true);
  await assert.rejects(f.flow.backupStatus(),/Unsupported Wi-Fi command/);assert.deepEqual(f.commands,[]);
  assert.equal((await f.flow.status()).revision,'6');assert.deepEqual(f.commands,['status']);
 }finally{f.close();}
});
test('capability is cleared when a connection ends and a legacy device reconnects',async()=>{
 const f=fixture();try{
  await f.connection.connect();await f.flow.backupStatus();f.close();f.setCapability(false);
  const info=await f.connection.connect();assert.notEqual(info.backup_status,true);
  await assert.rejects(f.flow.backupStatus(),/Unsupported Wi-Fi command/);assert.deepEqual(f.commands,['backup_status']);
 }finally{f.close();}
});
test('a wire Info field alone cannot bypass absent advertised capability',async()=>{
 const f=fixture({capability:false,infoCapability:true});try{
  await f.connection.connect();await assert.rejects(f.flow.backupStatus(),/Unsupported Wi-Fi command/);assert.deepEqual(f.commands,[]);
 }finally{f.close();}
});
test('backup_status cannot be dispatched before private authentication',async()=>{
 const f=fixture();try{await assert.rejects(f.flow.backupStatus(),/Connect to your Cloud Stick first/);assert.deepEqual(f.commands,[]);}finally{f.close();}
});
test('failed authentication clears backup admission despite advertised support',async()=>{
 const f=fixture({failAuthentication:true});try{
  await assert.rejects(f.connection.connect(),/authentication failure/);
  await assert.rejects(f.flow.backupStatus(),/Connect to your Cloud Stick first/);assert.deepEqual(f.commands,[]);
 }finally{f.close();}
});
for(const capability of ['true',1,null])test('invalid advertised capability '+JSON.stringify(capability)+' fails before commands',async()=>{
 const f=fixture({capability});try{await assert.rejects(f.connection.connect(),/Invalid backup capability/);assert.deepEqual(f.commands,[]);}finally{f.close();}
});
test('backup admission preserves unknown-operation and reserved-field rejection',async()=>{
 const f=fixture();try{
  await f.connection.connect();await assert.rejects(f.connection.command('backup_status',{op:'status'}),/Unsupported Wi-Fi command/);
  await assert.rejects(f.connection.command('unknown'),/Unsupported Wi-Fi command/);assert.deepEqual(f.commands,[]);
 }finally{f.close();}
});
test('late dedicated status cannot complete after actual connection teardown',async()=>{
 const f=fixture({delayBackupRead:true});try{
  await f.connection.connect();const pending=f.flow.backupStatus();await until(()=>f.awaitingRead);f.close();f.releaseRead();
  await assert.rejects(pending,/Connection was closed/);assert.deepEqual(f.commands,['backup_status']);
  assert.notEqual(f.states.at(-1).state,'connected');
 }finally{f.close();}
});
