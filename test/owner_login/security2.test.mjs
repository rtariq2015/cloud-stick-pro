// Synthetic-only production browser crypto against actual SDK and COW2 Store.
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {sdkHost} from '../../phone/milestone1/test/sdk_host.mjs';
import {Security2Client} from '../../phone/wifi-setup/security2.mjs';
const h=sdkHost(process.env.CSP_OWNER_LOGIN_SECURITY2_HOST);
const oldUser='csp-owner',oldPassword='PUBLIC-SYNTHETIC-OLD-OWNER-ONLY';
const newUser='operator-synthetic',newPassword='PUBLIC-SYNTHETIC-CUSTOM-LOGIN-42';
async function authenticate(user,password){
 const client=new Security2Client(user,password,{cryptoProvider:webcrypto});
 try{let answer=await h.call('session',await client.start());assert.equal(answer.error,0);
  answer=await h.call('session',await client.challenge(Uint8Array.from(Buffer.from(answer.data,'hex'))));assert.equal(answer.error,0);
  client.finish(Uint8Array.from(Buffer.from(answer.data,'hex')));return client;
 }catch(error){client.destroy();throw error;}
}
const reset=async()=>assert.equal((await h.call('reset')).error,0);
try{
 let client=await authenticate(oldUser,oldPassword);
 const stale=await client.encrypt(new TextEncoder().encode('synthetic-pre-rotation'));client.destroy();
 const saved=await h.call('rotate');assert.equal(saved.error,0);assert.equal(saved.writes,1);
 assert.notEqual((await h.call('decrypt',stale)).error,0);await reset();
 await assert.rejects(authenticate(oldUser,oldPassword));await reset();
 await assert.rejects(authenticate(oldUser,newPassword));await reset();
 await assert.rejects(authenticate(newUser,oldPassword));await reset();
 for(let i=0;i<3;i++){
  client=await authenticate(newUser,newPassword);const plain=new TextEncoder().encode('synthetic-post-rotation');
  const answer=await h.call('decrypt',await client.encrypt(plain));assert.equal(answer.error,0);assert.deepEqual(Buffer.from(answer.data,'hex'),Buffer.from(plain));
  const reply=new TextEncoder().encode('synthetic-status-only');const encrypted=await h.call('encrypt',reply);assert.equal(encrypted.error,0);
  assert.deepEqual(await client.decrypt(Uint8Array.from(Buffer.from(encrypted.data,'hex'))),reply);client.destroy();await reset();
 }
 console.log('PASS actual COW2 CAS rotation and pinned Security2: old proof/key rejected, custom username/password authenticated, three AEAD reconnects');
}finally{await h.close();}
