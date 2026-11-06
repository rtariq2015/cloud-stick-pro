import {test} from 'node:test';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {resolve} from 'node:path';
import {webcrypto} from 'node:crypto';
import {sdkHost} from '../../phone/milestone1/test/sdk_host.mjs';
import {ManagementConnection,WifiSetupClient,WifiInputError,SERVICE,VERSION,SESSION,INFO,WIFI} from '../../phone/wifi-setup/connection.mjs';
import {Security2Client,ProtocolError} from '../../phone/wifi-setup/security2.mjs';
const output=process.env.CSP_WIFI_TEST_OUT?resolve(process.env.CSP_WIFI_TEST_OUT):fileURLToPath(new URL('../../artifacts/wifi-integration/',import.meta.url));
const binary=resolve(output,'sdk-runtime-host');
const fixture=resolve(output,'synthetic-owner-record.bin');
const password='CSP-PUBLIC-SYNTHETIC-OWNER-ONLY-2026';
const enc=new TextEncoder(),decode=new TextDecoder();
const bytes=r=>Uint8Array.from(Buffer.from(r.data,'hex'));
function setup({key=password}={}){
 const host=sdkHost(binary,[fixture]),events=new Map();let last=null;
 const characteristics=Object.fromEntries([[VERSION,'version'],[SESSION,'session'],[INFO,'info'],[WIFI,'wifi']].map(([uuid,op])=>{
  let cache=new Uint8Array();return [uuid,{async writeValueWithResponse(data){
   assert(data.length<=512,'GATT writes must fit the 512-byte attribute limit');
   if(op==='version')assert.equal(decode.decode(data),'---');
   const r=await host.call(op,op==='version'?undefined:data);last=r;if(r.error)throw new ProtocolError('Device rejected the secure request.');cache=bytes(r);assert(cache.length<=512,'GATT reads must fit the 512-byte attribute limit');
  },async readValue(){return new DataView(cache.buffer,cache.byteOffset,cache.byteLength);},clear(){cache=new Uint8Array();}}];
 }));
 const gatt={connected:false,async connect(){this.connected=true;return {async getPrimaryService(service){assert.equal(service,SERVICE);return {async getCharacteristic(uuid){assert(characteristics[uuid]);return characteristics[uuid];}};}};},disconnect(){this.connected=false;for(const c of Object.values(characteristics))c.clear();}};
 const device={gatt,addEventListener:(e,fn)=>events.set(e,fn),removeEventListener:e=>events.delete(e)};
 const connection=new ManagementConnection({secureContext:true,bluetooth:{async requestDevice(options){assert.equal(options.filters[0].namePrefix,'CSP-W2-');return device;}},
  securityFactory:()=>new Security2Client('csp-owner',key,{cryptoProvider:webcrypto})});
 return {host,connection,flow:new WifiSetupClient(connection),get last(){return last;},async close(){connection.disconnect();await host.close();}};
}
test('actual owner Security2 + empty metadata cache + real runtime scan/check/durable save',{timeout:30000},async()=>{
 const f=setup();try{
  const info=await f.connection.connect();assert.equal(info.synthetic,false);assert.match(info.device_id,/CSP-W2-/);
  const previous=await f.flow.profiles();assert.equal(previous.length,1);assert.equal(previous[0].ssid,'CSP Previous Network');assert(previous[0].has_secret);
  const rows=await f.flow.scan();assert.equal(rows.length,2);assert(rows[1].open);
  await f.flow.stage({ssid:rows[0].ssid,password:'synthetic-wifi-123'});
  const checked=await f.flow.check();assert.equal(checked.internet_verified,false);assert.equal(checked.cloud_available,false);
  const saved=await f.flow.save();assert(saved.durable);assert.equal(saved.revision,'2');
  const state=await f.flow.status();assert.equal(state.revision,'2');assert.equal(state.applied_revision,'2');assert(state.durable);
  assert.equal((await f.flow.profiles()).length,2);assert(f.last.legacy_unchanged);assert(f.last.usb_polls>0);
 }finally{await f.close();}
});
test('open network selection is explicit; no Wi-Fi secret returned',{timeout:30000},async()=>{
 const f=setup();try{await f.connection.connect();await f.flow.profiles();await f.flow.stage({ssid:'CSP Test Open',open:true});
  await f.flow.check();await f.flow.save();const profiles=await f.flow.profiles();assert.equal(profiles.at(-1).has_secret,false);
  assert(!JSON.stringify(profiles).includes('synthetic-wifi'));assert(f.last.legacy_unchanged);
 }finally{await f.close();}
});
test('eight profiles and worst escaped SSIDs/passwords stay within real GATT limits',{timeout:30000},async()=>{
 const f=setup();try{
  await f.connection.connect();await f.flow.profiles();
  for(let i=0;i<7;++i){
   await f.flow.stage({ssid:'"'.repeat(31)+i,password:'\\'.repeat(63)});
   await f.flow.check();await f.flow.save();
  }
  const profiles=await f.flow.profiles();assert.equal(profiles.length,8);
  assert(profiles.every(p=>!Object.hasOwn(p,'password')&&!Object.hasOwn(p,'secret')));
  await assert.rejects(f.flow.stage({ssid:'CSP ninth',password:'synthetic-password'}),e=>e instanceof WifiInputError&&e.code===7);
  await f.flow.abort();assert.equal((await f.flow.profiles()).length,8);assert(f.last.legacy_unchanged);
 }finally{await f.close();}
});
test('wrong private key and public M1 default never authorize settings',{timeout:30000},async()=>{
 for(const key of ['incorrect-owner-secret-test','abcd1234']){
  const f=setup({key});try{await assert.rejects(f.connection.connect());assert.equal(f.last.writes,0);assert(f.last.terminal);}
  finally{await f.close();}
 }
});
test('wrong Wi-Fi result preserves old active revision and cannot save',{timeout:30000},async()=>{
 const f=setup();try{await f.connection.connect();await f.flow.profiles();await f.host.call('radio-fail');
  await f.flow.stage({ssid:'CSP Test WiFi',password:'synthetic-wifi-123'});
  await assert.rejects(f.flow.check(),error=>error instanceof WifiInputError&&error.code===14);
  await assert.rejects(f.flow.save(),error=>error instanceof WifiInputError);
  const state=await f.flow.status();assert.equal(state.revision,'1');assert.equal(f.last.writes,0);assert(f.last.legacy_unchanged);
 }finally{await f.close();}
});
test('NVS full/failure is never acknowledged as saved',{timeout:30000},async()=>{
 const f=setup();try{await f.connection.connect();await f.flow.profiles();await f.flow.stage({ssid:'CSP Test WiFi',password:'synthetic-wifi-123'});
  await f.flow.check();await f.host.call('fail-write');await assert.rejects(f.flow.save(),error=>error instanceof WifiInputError);
  assert.equal((await f.flow.status()).revision,'1');assert(f.last.legacy_unchanged);
 }finally{await f.close();}
});
test('readable selection after failed NVS commit is uncertain, never durable success',{timeout:30000},async()=>{
 const f=setup();try{await f.connection.connect();await f.flow.profiles();await f.flow.stage({ssid:'CSP Test WiFi',password:'synthetic-wifi-123'});
  await f.flow.check();await f.host.call('lost-active-ack');await assert.rejects(f.flow.save(),error=>error instanceof WifiInputError);
  await assert.rejects(f.flow.status(),error=>error instanceof WifiInputError&&error.code===6);assert(f.last.legacy_unchanged);
 }finally{await f.close();}
});
test('disconnect before explicit commit aborts staged settings',{timeout:30000},async()=>{
 const f=setup();try{await f.connection.connect();await f.flow.profiles();await f.flow.stage({ssid:'CSP Test WiFi',password:'synthetic-wifi-123'});
  await f.flow.check();f.connection.disconnect();await f.host.call('close');await f.host.call('window');
  await f.connection.connect();assert.equal((await f.flow.status()).revision,'1');assert.equal(f.last.writes,0);
 }finally{await f.close();}
});
async function rawAuthentication(host){
 const client=new Security2Client('csp-owner',password,{cryptoProvider:webcrypto});
 let r=await host.call('session',await client.start());assert.equal(r.error,0);
 r=await host.call('session',await client.challenge(bytes(r)));assert.equal(r.error,0);client.finish(bytes(r));
 r=await host.call('info',await client.encrypt(enc.encode('{"v":1,"id":1,"op":"info"}')));assert.equal(r.error,0);await client.decrypt(bytes(r));return client;
}
test('malformed/tag/oversized authenticated messages latch denial before owner poll',{timeout:30000},async()=>{
 for(const failure of ['malformed','tag','size']){
  const h=sdkHost(binary,[fixture]);let client;
  try{client=await rawAuthentication(h);
   let payload=failure==='malformed'?'{"v":2,"id":2,"op":"scan","owner":true}':failure==='size'?' '.repeat(513):'{"v":2,"id":2,"op":"status"}';
   const bad=await client.encrypt(enc.encode(payload));if(failure==='tag')bad[0]^=1;
   let r=await h.call('wifi-no-poll',bad);assert.notEqual(r.error,0);assert(r.terminal);assert.equal(r.writes,0);
   r=await h.call('wifi-no-poll',await client.encrypt(enc.encode('{"v":2,"id":3,"op":"scan"}')));
   assert.notEqual(r.error,0);assert(r.terminal);assert.equal(r.writes,0);assert(r.legacy_unchanged);
  }finally{client?.destroy();await h.close();}
 }
});
