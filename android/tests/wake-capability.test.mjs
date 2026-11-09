/* SPDX-License-Identifier: MIT */
import test from 'node:test';
import assert from 'node:assert/strict';
import {ManagementConnection,validateMetadata,VERSION,SESSION,INFO,WIFI} from '../app/src/main/assets/shared/connection.mjs';
const encode=new TextEncoder(),decode=new TextDecoder();
const baseline={prov:{sec_ver:2,sec_patch_ver:1},csp:{milestone:2,synthetic:false,owner:'card',wifi:true,cloud:false}};
function metadata(extra={}){return {...baseline,csp:{...baseline.csp,...extra}};}
function fixture(capability){
 let current=metadata(capability),last=new Map(),writes=[],ended=new Set();
 const characteristic=uuid=>({async writeValueWithResponse(bytes){last.set(uuid,bytes.slice());if(uuid===WIFI)writes.push(JSON.parse(decode.decode(bytes)));},async readValue(){
  const input=last.get(uuid);let answer;
  if(uuid===VERSION)answer=current;
  else if(uuid===SESSION)return new Uint8Array([1]);
  else {const request=JSON.parse(decode.decode(input));answer=uuid===INFO?{v:1,id:request.id,ok:true,model:'synthetic',device_id:'public-synthetic-id',firmware:'test',wifi_connected:true,backups_paused:true,synthetic:false}:{v:2,id:request.id,ok:true,job:1,pending:false,data:{ok:true,durable:true,wake_id:'a'.repeat(32)}};}
  return encode.encode(JSON.stringify(answer));
 }});
 const device={addEventListener:(_,fn)=>ended.add(fn),removeEventListener:(_,fn)=>ended.delete(fn),gatt:{async connect(){return this;},async getPrimaryService(){return {async getCharacteristic(uuid){return characteristic(uuid);}};},disconnect(){}}};
 const secure=()=>({async start(){return new Uint8Array([1]);},async challenge(){return new Uint8Array([2]);},finish(){},async encrypt(v){return v.slice();},async decrypt(v){return v.slice();},destroy(){}});
 const connection=new ManagementConnection({bluetooth:{async requestDevice(){return device;}},secureContext:true,securityFactory:secure,sessionTimeoutMs:null});
 return {connection,writes,setMetadata:extra=>{current=metadata(extra);}};
}
test('wake metadata requires explicit typed version-one capability',()=>{
 assert.equal(validateMetadata(metadata()).csp.ble_wake,undefined);assert.equal(validateMetadata(metadata({ble_wake:true,ble_wake_protocol:1})).csp.ble_wake,true);
 for(const extra of [{ble_wake:'true'},{ble_wake:true},{ble_wake:true,ble_wake_protocol:2},{ble_wake_protocol:1},{ble_wake:false,ble_wake_protocol:1}])assert.throws(()=>validateMetadata(metadata(extra)));
});
test('pair command requires authenticated advertised capability and clears on reconnect to older firmware',async()=>{
 const f=fixture({});await assert.rejects(f.connection.command('wake_pair',{key:'04'+'a'.repeat(128)}));
 await f.connection.connect();await assert.rejects(f.connection.command('wake_pair',{key:'04'+'a'.repeat(128)}));assert.equal(f.writes.length,0);f.connection.disconnect();
 f.setMetadata({ble_wake:true,ble_wake_protocol:1});assert.equal((await f.connection.connect()).ble_wake,true);assert.equal((await f.connection.command('wake_pair',{key:'04'+'a'.repeat(128)})).durable,true);assert.equal(f.writes[0].op,'wake_pair');f.connection.disconnect();
 f.setMetadata({});assert.equal((await f.connection.connect()).ble_wake,undefined);await assert.rejects(f.connection.command('wake_pair',{key:'04'+'a'.repeat(128)}));assert.equal(f.writes.length,1);f.connection.disconnect();
});
