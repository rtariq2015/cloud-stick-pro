import {test} from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import {Security2Client,ProtocolError,field,fields} from '../app/src/main/assets/shared/security2.mjs';
const vector=JSON.parse(await readFile(new URL('./reference-vectors.json',import.meta.url)));
const shortVector=JSON.parse(await readFile(new URL('./reference-vectors-short-b.json',import.meta.url)));
const fromHex=s=>Uint8Array.from(Buffer.from(s,'hex'));
const hex=b=>Buffer.from(b).toString('hex');
const join=(...p)=>Uint8Array.from(Buffer.concat(p.map(x=>Buffer.from(x))));
const response=(msg,member,body)=>join(field(2,2),field(12,join(field(1,msg),field(member,body))));
const response0=()=>response(1,21,join(field(2,fromHex(vector.B)),field(3,fromHex(vector.salt))));
const response1=(proof=fromHex(vector.device_proof),nonce=fromHex(vector.nonce))=>response(3,23,join(field(2,proof),field(3,nonce)));
const client=()=>new Security2Client(vector.username,vector.password,{cryptoProvider:webcrypto,random:()=>fromHex(vector.a)});
async function challenged(c=client()) {await c.start();return {c,command:await c.challenge(response0())};}
async function ready(){const {c}=await challenged();c.finish(response1());return c;}
test('SRP public key matches independent official Python reference',async()=>{
 const c=client();const outer=fields(await c.start(),[2,12]);assert.equal(outer.get(2),2);
 const inner=fields(outer.get(12),[1,20]);const body=fields(inner.get(20),[1,2]);
 assert.equal(new TextDecoder().decode(body.get(1)),vector.username);assert.equal(hex(body.get(2)),vector.A);c.destroy();
});
test('SRP proof matches independent official Python reference',async()=>{
 const {c,command}=await challenged();const outer=fields(command,[2,12]),inner=fields(outer.get(12),[1,22]),body=fields(inner.get(22),[1]);assert.equal(hex(body.get(1)),vector.proof);c.finish(response1());c.destroy();
});
test('legitimate 383-byte server B matches the official reference proof',async()=>{
 const c=client();await c.start();const command=await c.challenge(response(1,21,join(field(2,fromHex(shortVector.B)),field(3,fromHex(shortVector.salt)))));
 const outer=fields(command,[2,12]),inner=fields(outer.get(12),[1,22]),body=fields(inner.get(22),[1]);assert.equal(hex(body.get(1)),shortVector.proof);
 c.finish(response1(fromHex(shortVector.device_proof)));c.destroy();
});
test('wrong ownership password cannot accept correct device proof',async()=>{
 const c=new Security2Client(vector.username,'incorrect-synthetic',{cryptoProvider:webcrypto,random:()=>fromHex(vector.a)});
 await challenged(c);assert.throws(()=>c.finish(response1()),/ownership proof/);await assert.rejects(c.encrypt(new Uint8Array()),/not ready/);
});
test('leading-zero salt encoding fails closed in the synthetic-only prototype',async()=>{
 const c=client();await c.start();const salt=fromHex(vector.salt);salt[0]=0;await assert.rejects(c.challenge(response(1,21,join(field(2,fromHex(vector.B)),field(3,salt)))),/leading-zero/);await assert.rejects(c.encrypt(Uint8Array.of(1)),/not ready/);
});
test('unsupported credential hash encoding is rejected rather than normalized',async()=>{
 const c=new Security2Client(vector.username,vector.unsupported_password,{cryptoProvider:webcrypto,random:()=>fromHex(vector.a)});await c.start();await assert.rejects(c.challenge(response0()),/credential encoding/);
});
test('missing, modified or short device proof fails closed',async()=>{
 for(const p of [new Uint8Array(),new Uint8Array(64),fromHex(vector.device_proof).slice(1)]){const {c}=await challenged();assert.throws(()=>c.finish(response1(p)),/ownership proof/);}
});
test('legacy/static nonce and unsupported security scheme are rejected',async()=>{
 for(const n of [new Uint8Array(16),new Uint8Array(),new Uint8Array(11)]){const {c}=await challenged();assert.throws(()=>c.finish(response1(fromHex(vector.device_proof),n)),/patch 1/);}
 const c=client();await c.start();await assert.rejects(c.challenge(join(field(2,1),field(12,new Uint8Array()))),/Security 2/);
});
test('malformed, oversized and duplicate protobuf fields are rejected',async()=>{
 for(const p of [new Uint8Array(1025),Uint8Array.of(18,99),join(field(2,2),field(2,2)),Uint8Array.of(16,128,128,128,128,128)]){const c=client();await c.start();await assert.rejects(c.challenge(p),ProtocolError);}
});
test('invalid SRP server public value is rejected',async()=>{
 const c=client();await c.start();await assert.rejects(c.challenge(response(1,21,join(field(2,new Uint8Array(384)),field(3,fromHex(vector.salt))))),/server key/);
});
test('patch1 AES-GCM matches WebCrypto and advances one shared counter',async()=>{
 const c=await ready(),key=await webcrypto.subtle.importKey('raw',fromHex(vector.key),{name:'AES-GCM'},false,['encrypt']);
 const payload=new TextEncoder().encode('{"v":1,"id":1,"op":"info"}');
 const expected=new Uint8Array(await webcrypto.subtle.encrypt({name:'AES-GCM',iv:fromHex(vector.nonce)},key,payload));assert.deepEqual(await c.encrypt(payload),expected);
 const nonce=fromHex(vector.nonce);new DataView(nonce.buffer).setUint32(8,2,false);
 const answer=new TextEncoder().encode('{"ok":true}'),cipher=new Uint8Array(await webcrypto.subtle.encrypt({name:'AES-GCM',iv:nonce},key,answer));assert.deepEqual(await c.decrypt(cipher),answer);c.destroy();
});
test('tamper and replay fail authentication and close the client',async()=>{
 for(const replay of [false,true]){const c=await ready(),key=await webcrypto.subtle.importKey('raw',fromHex(vector.key),{name:'AES-GCM'},false,['encrypt']);const cipher=new Uint8Array(await webcrypto.subtle.encrypt({name:'AES-GCM',iv:fromHex(vector.nonce)},key,Uint8Array.of(1,2)));if(replay){assert.deepEqual(await c.decrypt(cipher),Uint8Array.of(1,2));}else cipher[0]^=1;await assert.rejects(c.decrypt(cipher),/failed verification/);await assert.rejects(c.encrypt(Uint8Array.of(1)),/not ready/);}
});
test('out-of-order steps, destroy and repeated independent sessions are safe',async()=>{
 const c=client();assert.throws(()=>c.finish(response1()),/Unexpected/);c.destroy();await assert.rejects(c.start(),/new authentication/);
 for(let i=0;i<5;i++){const next=await ready();next.destroy();await assert.rejects(next.encrypt(Uint8Array.of(1)),/not ready/);}
});
test('nonce exhaustion and oversize plaintext close the secure session',async()=>{
 const {c}=await challenged(),nonce=fromHex(vector.nonce);new DataView(nonce.buffer).setUint32(8,0xffffffff,false);c.finish(response1(fromHex(vector.device_proof),nonce));await assert.rejects(c.encrypt(Uint8Array.of(1)),/counter exhausted/);
 const next=await ready();await assert.rejects(next.encrypt(new Uint8Array(1024)),/too large/);await assert.rejects(next.encrypt(Uint8Array.of(1)),/not ready/);
});
test('disconnect during asynchronous authentication cannot resurrect its keys',async()=>{
 const c=client();await c.start();const pending=c.challenge(response0());c.destroy();await assert.rejects(pending,/closed/);await assert.rejects(c.encrypt(Uint8Array.of(1)),/not ready/);
});
test('disconnect during asynchronous encryption discards the result',async()=>{
 const c=await ready(),pending=c.encrypt(Uint8Array.of(1));c.destroy();await assert.rejects(pending,/closed/);await assert.rejects(c.encrypt(Uint8Array.of(1)),/not ready/);
});
