// Real pinned Security2 host from an explicitly selected baseline build.
// Its NVS/radio/USB backends are synthetic. It does not implement new login
// writes; this covers configurable credentials through the existing SDK crypto.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {readFileSync,mkdirSync,writeFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {sdkHost} from '../../phone/milestone1/test/sdk_host.mjs';
import {ManagementConnection,SERVICE,VERSION,SESSION,INFO,WIFI} from '../../phone/wifi-setup/connection.mjs';
import {ProtocolError} from '../../phone/wifi-setup/security2.mjs';
globalThis.crypto??=webcrypto;
const vectors=JSON.parse(readFileSync(new URL('./reference-vectors.json',import.meta.url))).vectors;
const binary=process.env.CSP_OWNER_LOGIN_SDK_HOST;
const output=resolve('artifacts/owner-login-phone/sdk-interop');
const decode=new TextDecoder();
function fixture(vector,{username=vector.username,password=vector.password,legacy=false}={}){
 mkdirSync(output,{recursive:true});const path=resolve(output,'PUBLIC-SYNTHETIC-'+vector.username+'.bin');
 writeFileSync(path,Buffer.from(vector.record,'hex'),{mode:0o600});
 const host=sdkHost(binary,[path]),events=new Map();let last;
 const characteristics=Object.fromEntries([[VERSION,'version'],[SESSION,'session'],[INFO,'info'],[WIFI,'wifi']].map(([id,operation])=>{let cache=new Uint8Array();return [id,{
  async writeValueWithResponse(data){assert(data.length<=512);if(operation==='version')assert.equal(decode.decode(data),'---');
   const answer=await host.call(operation,operation==='version'?undefined:data);last=answer;if(answer.error)throw new ProtocolError('Synthetic SDK rejected request.');cache=Uint8Array.from(Buffer.from(answer.data,'hex'));},
  async readValue(){return new DataView(cache.buffer,cache.byteOffset,cache.byteLength);},clear(){cache=new Uint8Array();}}];}));
 const device={gatt:{connected:false,async connect(){this.connected=true;return {async getPrimaryService(id){assert.equal(id,SERVICE);return {async getCharacteristic(id){return characteristics[id];}};}};},
  disconnect(){this.connected=false;for(const characteristic of Object.values(characteristics))characteristic.clear();}},addEventListener:(name,fn)=>events.set(name,fn),removeEventListener:name=>events.delete(name)};
 const connection=new ManagementConnection({secureContext:true,bluetooth:{requestDevice:async()=>device},...(legacy?{ownerKey:password}:{username,password})});
 return {connection,get last(){return last;},async close(){connection.disconnect();await host.close();}};
}
for(const vector of vectors)test('configurable '+vector.username+' authenticates through actual pinned SDK',{skip:!binary,timeout:30000},async()=>{
 const f=fixture(vector);try{const info=await f.connection.connect();assert.equal(info.synthetic,false);assert.match(info.device_id,/CSP-W2-/);assert.equal(f.last.writes,0);assert(f.last.legacy_unchanged);}
 finally{await f.close();}
});
test('wrong configurable username or password does not authenticate or write',{skip:!binary,timeout:30000},async()=>{
 for(const overrides of [{username:'different-owner'},{password:'PUBLIC-WRONG-PASSWORD'}]){
  const f=fixture(vectors[0],overrides);try{await assert.rejects(f.connection.connect());assert.equal(f.last.writes,0);assert(f.last.legacy_unchanged);}
  finally{await f.close();}
 }
});
test('legacy key constructor still authenticates unchanged csp-owner records',{skip:!binary,timeout:30000},async()=>{
 const f=fixture(vectors.find(vector=>vector.username==='csp-owner'),{legacy:true});try{await f.connection.connect();assert.equal(f.last.writes,0);assert(f.last.legacy_unchanged);}
 finally{await f.close();}
});
