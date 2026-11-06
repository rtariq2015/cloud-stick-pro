// Exercise the browser port against pinned ESP-IDF C with the shared reviewed
// alignment correction. Desktop execution does not prove actual GATT/radio.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {sdkHost} from './sdk_host.mjs';
import {existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {webcrypto} from 'node:crypto';
import {Security2Client,field} from '../security2.mjs';
const binary=process.env.CSP_SECURITY2_HOST||fileURLToPath(new URL('../../../artifacts/milestone1/host-security2/security2-host-asan',import.meta.url));
const encoder=new TextEncoder();
const host=()=>sdkHost(binary);
async function authenticate(h,password='abcd1234') {
 const c=new Security2Client('wifiprov',password,{cryptoProvider:webcrypto});
 let r=await h.call('session',await c.start());assert.equal(r.error,0);
 const command=await c.challenge(Uint8Array.from(Buffer.from(r.data,'hex')));
 r=await h.call('session',command);if(r.error){c.destroy();return {error:r.error};}
 c.finish(Uint8Array.from(Buffer.from(r.data,'hex')));return {c,error:0};
}
const skip=!existsSync(binary);
test('actual SDK Security2 authenticates browser port and exchanges AEAD messages',{skip},async()=>{
 const h=host();try{const {c,error}=await authenticate(h);assert.equal(error,0);
 const plain=encoder.encode('{"v":1,"id":1,"op":"info"}');let r=await h.call('decrypt',await c.encrypt(plain));assert.equal(r.error,0);assert.deepEqual(Buffer.from(r.data,'hex'),Buffer.from(plain));
 const answer=encoder.encode('{"v":1,"id":1,"ok":true}');r=await h.call('encrypt',answer);assert.equal(r.error,0);assert.deepEqual(await c.decrypt(Uint8Array.from(Buffer.from(r.data,'hex'))),answer);c.destroy();}finally{await h.close();}
});
test('actual SDK rejects wrong ownership proof',{skip},async()=>{
 const h=host();try{assert.notEqual((await authenticate(h,'incorrect-synthetic')).error,0);}finally{await h.close();}
});
test('actual SDK rejects modified GCM data and replay',{skip},async()=>{
 for(const replay of [false,true]){const h=host();try{const {c}=await authenticate(h),cipher=await c.encrypt(encoder.encode('synthetic'));
 if(replay){assert.equal((await h.call('decrypt',cipher)).error,0);}else cipher[0]^=1;
 assert.notEqual((await h.call('decrypt',cipher)).error,0);c.destroy();}finally{await h.close();}}
});
test('actual SDK rejects malformed sessions and Security0 downgrade',{skip},async()=>{
 for(const data of [Uint8Array.of(18,255),field(2,0),Uint8Array.of(0xff,0xff,0xff)]){const h=host();try{assert.notEqual((await h.call('session',data)).error,0);}finally{await h.close();}}
});
test('actual SDK reset invalidates old keys; repeated synthetic sessions authenticate',{skip},async()=>{
 const h=host();try{let {c}=await authenticate(h);const old=await c.encrypt(encoder.encode('before-disconnect'));c.destroy();assert.equal((await h.call('reset')).error,0);assert.notEqual((await h.call('decrypt',old)).error,0);
 for(let i=0;i<10;i++){assert.equal((await h.call('reset')).error,0);({c}=await authenticate(h));assert(c);c.destroy();}
 }finally{await h.close();}
});
