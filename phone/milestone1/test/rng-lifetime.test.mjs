import {test} from 'node:test';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {webcrypto} from 'node:crypto';
import {sdkHost} from './sdk_host.mjs';
import {Security2Client} from '../security2.mjs';
const binary=process.env.CSP_RNG_TEST_BINARY||fileURLToPath(new URL('../../../artifacts/milestone1/host-security2/security2-host-rng-asan',import.meta.url));
async function authenticate(h,password='abcd1234'){
 const c=new Security2Client('wifiprov',password,{cryptoProvider:webcrypto});
 try{
  let r=await h.call('session',await c.start());assert.equal(r.error,0);
  const proof=await c.challenge(Uint8Array.from(Buffer.from(r.data,'hex')));
  r=await h.call('session',proof);
  if(r.error===0)c.finish(Uint8Array.from(Buffer.from(r.data,'hex')));
  return r.error;
 }finally{c.destroy();}
}
async function balanced(h,count){
 const s=await h.call('rng-status');
 assert.equal(s.entropy_live,0);assert.equal(s.drbg_live,0);
 assert.equal(s.entropy_init,count);assert.equal(s.entropy_free,count);
 assert.equal(s.drbg_init,count);assert.equal(s.drbg_free,count);
}
test('real SDK releases temporary RNG contexts after 25 authenticated sessions',async()=>{
 const h=sdkHost(binary);
 try{for(let i=1;i<=25;i++){assert.equal(await authenticate(h),0);await balanced(h,i);assert.equal((await h.call('reset')).error,0);}}
 finally{await h.close();}
});
for(const mode of ['seed-failure','random-failure'])test('real SDK releases RNG contexts after '+mode+' and can retry',async()=>{
 const h=sdkHost(binary,[mode]);
 try{
  assert.notEqual(await authenticate(h),0);await balanced(h,1);
  assert.equal((await h.call('reset')).error,0);
  assert.equal(await authenticate(h),0);await balanced(h,2);
 }finally{await h.close();}
});
test('wrong ownership proof does not initialize temporary RNG contexts',async()=>{
 const h=sdkHost(binary);
 try{assert.notEqual(await authenticate(h,'wrong-public-test-password'),0);await balanced(h,0);}
 finally{await h.close();}
});
