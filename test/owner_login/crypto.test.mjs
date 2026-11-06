import {test} from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto,createHash} from 'node:crypto';
import {readFileSync} from 'node:fs';
import {createOwnerRecord,validateOwnerLogin,OwnerRecordError,ProtocolError,Security2Client,field,fields} from '../../phone/wifi-setup/security2.mjs';
const data=JSON.parse(readFileSync(new URL('./reference-vectors.json',import.meta.url)));
const hex=value=>Buffer.from(value).toString('hex'),fromHex=value=>Uint8Array.from(Buffer.from(value,'hex'));
const join=(...parts)=>Uint8Array.from(Buffer.concat(parts.map(value=>Buffer.from(value))));
const response=(message,member,body)=>join(field(2,2),field(12,join(field(1,message),field(member,body))));
for(const vector of data.vectors)test('canonical COW2 matches pinned official Python golden for '+vector.username,async()=>{
 const salt=fromHex(vector.salt),record=await createOwnerRecord({...vector,cryptoProvider:webcrypto,random:()=>salt});
 assert.equal(record.length,432);assert.equal(hex(record),vector.record);assert.equal(createHash('sha256').update(record).digest('hex'),vector.sha256);
 assert.deepEqual(record.slice(412,428),new Uint8Array(16));assert(salt.every(value=>value===0),'Salt working buffer cleared after generation');
 assert(!Buffer.from(record).includes(Buffer.from(vector.password)));record.fill(0);
});
test('leading-zero inner identity hash is explicitly rejected, without a different SRP equation or a record',async()=>{
 const vector=data.rare_inner_zero;assert(vector.inner_sha512.startsWith('00'));assert.notEqual(vector.reference_verifier,vector.sdk_raw_verifier);
 let randomCalls=0;await assert.rejects(createOwnerRecord({...vector,device:data.vectors[0].device,cryptoProvider:webcrypto,random:()=>{++randomCalls;return fromHex(data.vectors[0].salt);}}),
  error=>error instanceof OwnerRecordError&&error.code==='encoding');assert.equal(randomCalls,0);
});
test('new password policy is separate from existing-login compatibility',()=>{
 assert.doesNotThrow(()=>validateOwnerLogin('owner','oldpass8'));
 assert.throws(()=>validateOwnerLogin('owner','oldpass8',{newPassword:true}),OwnerRecordError);
 assert.doesNotThrow(()=>validateOwnerLogin('admin','é'.repeat(6),{newPassword:true}));
 assert.doesNotThrow(()=>validateOwnerLogin('A'.repeat(32),'P'.repeat(128),{newPassword:true}));
 for(const [username,password] of [['','PUBLIC-SYNTHETIC-PASS'],['x'.repeat(33),'PUBLIC-SYNTHETIC-PASS'],['a:b','PUBLIC-SYNTHETIC-PASS'],['a b','PUBLIC-SYNTHETIC-PASS'],['é','PUBLIC-SYNTHETIC-PASS'],
  ['admin','P'.repeat(129)],['admin','é'.repeat(65)],['admin','invalid\npassword'],['admin','invalid\u0000password'],['admin','\ud800'.repeat(12)]])assert.throws(()=>validateOwnerLogin(username,password,{newPassword:true}),OwnerRecordError);
});
test('canonical generator rejects invalid device and missing secure crypto before a record can be staged',async()=>{
 const vector=data.vectors[0];for(const device of ['00112233445A','00:11:22:33:44:55','',null])await assert.rejects(createOwnerRecord({...vector,device,cryptoProvider:webcrypto}),ProtocolError);
 await assert.rejects(createOwnerRecord({...vector,cryptoProvider:{}}),/cryptography/);
});
test('noncanonical leading-zero salt is rerolled, never normalized or hard-coded',async()=>{
 let calls=0;const vector=data.vectors[0],zero=new Uint8Array(16),salt=fromHex(vector.salt);
 const record=await createOwnerRecord({...vector,cryptoProvider:webcrypto,random:()=>++calls===1?zero:salt});
 assert.equal(calls,2);assert.equal(hex(record),vector.record);assert(zero.every(value=>value===0));assert(salt.every(value=>value===0));record.fill(0);
});
test('invalid RNG output and exhausted canonical salt attempts fail closed',async()=>{
 const vector=data.vectors[0];for(const value of [null,{},new Uint8Array(15)])await assert.rejects(createOwnerRecord({...vector,cryptoProvider:webcrypto,random:()=>value}),/random source/);
 let calls=0;await assert.rejects(createOwnerRecord({...vector,cryptoProvider:webcrypto,random:()=>{++calls;return new Uint8Array(16);}}),/canonical Security2/);assert.equal(calls,128);
});
test('asynchronous digest completion after cancellation cannot return an owner record',async()=>{
 const vector=data.vectors[0];let release,cancelled=false;
 const cryptoProvider={getRandomValues:webcrypto.getRandomValues.bind(webcrypto),subtle:{async digest(...args){await new Promise(resolve=>release=resolve);return webcrypto.subtle.digest(...args);}}};
 const pending=createOwnerRecord({...vector,cryptoProvider,cancelled:()=>cancelled});await Promise.resolve();cancelled=true;release();await assert.rejects(pending,/cancelled/);
});
test('Security2 handshake/cipher source remains exactly the frozen reviewed module outside the new helper',()=>{
 const source=readFileSync(new URL('../../phone/wifi-setup/security2.mjs',import.meta.url),'utf8');
 const baseline=source.replace(/\/\/ BEGIN OWNER LOGIN RECORD V1\n[\s\S]*?\/\/ END OWNER LOGIN RECORD V1\n\n/,'');
 assert.equal(createHash('sha256').update(baseline).digest('hex'),'83c66d50af453477f5bdba1cf70faada5332b9d16f9575afbd6a7436eda6a6ff');
});
test('served Security2 still matches official proof, enforces server proof and rejects replay',async()=>{
 const vector=JSON.parse(readFileSync(new URL('../../phone/milestone1/test/reference-vectors.json',import.meta.url)));
 const client=new Security2Client(vector.username,vector.password,{cryptoProvider:webcrypto,random:()=>fromHex(vector.a)});
 const start=await client.start(),outer=fields(start,[2,12]),inner=fields(outer.get(12),[1,20]);
 assert.equal(new TextDecoder().decode(fields(inner.get(20),[1,2]).get(1)),vector.username);
 const proof=await client.challenge(response(1,21,join(field(2,fromHex(vector.B)),field(3,fromHex(vector.salt)))));
 assert.equal(hex(fields(fields(fields(proof,[2,12]).get(12),[1,22]).get(22),[1]).get(1)),vector.proof);
 client.finish(response(3,23,join(field(2,fromHex(vector.device_proof)),field(3,fromHex(vector.nonce)))));
 const key=await webcrypto.subtle.importKey('raw',fromHex(vector.key),{name:'AES-GCM'},false,['encrypt']);
 const cipher=new Uint8Array(await webcrypto.subtle.encrypt({name:'AES-GCM',iv:fromHex(vector.nonce)},key,Uint8Array.of(7)));
 assert.deepEqual(await client.decrypt(cipher),Uint8Array.of(7));await assert.rejects(client.decrypt(cipher),/failed verification/);client.destroy();
});
