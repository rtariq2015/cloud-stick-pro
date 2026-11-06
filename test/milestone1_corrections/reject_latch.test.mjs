import {test} from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {EventEmitter,once} from 'node:events';
import {createInterface} from 'node:readline';
import {PassThrough} from 'node:stream';
import {setImmediate} from 'node:timers/promises';
import {webcrypto} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {Security2Client} from '../../phone/milestone1/security2.mjs';
import {validateInfo} from '../../phone/milestone1/connection.mjs';
const binary=process.env.CSP_ADAPTER_HOST||fileURLToPath(new URL('../../artifacts/milestone1-corrections/adapter/actual-adapter-sdk-asan',import.meta.url));
const guided=process.env.CSP_GUIDED_SESSION==='1';
const raw=s=>Uint8Array.from(Buffer.from(s,'hex'));
const encoded=(id=1,op='info')=>new TextEncoder().encode(JSON.stringify({v:1,id,op}));
function host(spawnChild=spawn){
 const child=spawnChild(binary,[],{stdio:['pipe','pipe','pipe'],env:{...process.env,
   ASAN_OPTIONS:'detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS:'halt_on_error=1:print_stacktrace=1'}});
 let stderr='',exited=false;const queue=[];child.stderr.on('data',d=>{stderr+=d;});
 // Capture closure immediately: exit does not mean stdout/stderr have drained.
 const closed=new Promise(resolve=>child.once('close',resolve));
 createInterface({input:child.stdout}).on('line',line=>{
   const q=queue.shift();if(q){clearTimeout(q.timer);try{q.resolve(JSON.parse(line));}catch(e){q.reject(e);}}
 });
 child.on('error',error=>{for(const q of queue)q.reject(error);});
 child.on('exit',code=>{exited=true;for(const q of queue){clearTimeout(q.timer);q.reject(new Error('Host exit '+code+': '+stderr));}});
 return {
  call(command,data){return new Promise((resolve,reject)=>{
   const timer=setTimeout(()=>{child.kill();reject(new Error('host timeout'));},5000);
   queue.push({resolve,reject,timer});child.stdin.write(command+(data?.length?' '+Buffer.from(data).toString('hex'):'')+'\n');
  });},
  async close(){
   if(!exited&&!child.stdin.writableEnded)child.stdin.end('quit\n');
   const code=await closed;
   if(code!==0||stderr)throw new Error('Host cleanup '+code+': '+stderr);
  }
 };
}
// Model process exit before inherited output pipes finish, without timing races.
function hostFixture(){
 const child=new EventEmitter();
 Object.assign(child,{stdin:new PassThrough(),stdout:new PassThrough(),stderr:new PassThrough(),exitCode:null});
 const h=host(()=>child);
 return {h,child,exit(code=0){child.exitCode=code;child.emit('exit',code,null);},async finish(){
  const closed=Promise.all([once(child.stdout,'close'),once(child.stderr,'close')]);
  child.stdout.end();child.stderr.end();await closed;child.emit('close',child.exitCode,null);
 }};
}
for(const afterExit of [false,true])
for(const diagnostic of ['runtime error: late alignment diagnostic','ERROR: AddressSanitizer: late diagnostic'])
test(`host cleanup rejects late stderr with zero exit (cleanup called ${afterExit?'after':'before'} exit): ${diagnostic}`,async()=>{
 const f=hostFixture();
 if(afterExit)f.exit();
 const closing=f.h.close();
 if(!afterExit)f.exit();
 f.child.stderr.write(diagnostic+'\n');
 await Promise.all([assert.rejects(closing,/Host cleanup 0:.*late/s),f.finish()]);
});
test('host cleanup rejects nonzero exit after collecting late stderr',async()=>{
 const f=hostFixture(),closing=f.h.close();f.exit(7);f.child.stderr.write('late diagnostic\n');
 await Promise.all([assert.rejects(closing,/Host cleanup 7: late diagnostic/),f.finish()]);
});
test('host cleanup waits for clean output closure and remembers an already closed child',async()=>{
 const f=hostFixture();let settled=false;
 const closing=f.h.close().finally(()=>{settled=true;});f.exit();
 try{await setImmediate();assert.equal(settled,false,'exit alone must not complete cleanup');}
 finally{await f.finish();await closing;}
 await f.h.close();
});
async function authenticate(h){
 const c=new Security2Client('wifiprov','abcd1234',{cryptoProvider:webcrypto});
 let r=await h.call('session',await c.start());assert.equal(r.error,0);
 const command=await c.challenge(raw(r.data));r=await h.call('session',command);assert.equal(r.error,0);
 c.finish(raw(r.data));return c;
}
function denied(r){
 assert.notEqual(r.error,0);assert.equal(r.data,'');assert.equal(r.stopRequested,true);
}
for(const failure of ['bad GCM tag','invalid frame length','invalid application grammar'])
test(failure+' immediately denies valid callbacks and reconnect until a new physical window',async()=>{
 const h=host();let c=await authenticate(h);
 try{
  let valid,invalid;
  if(failure==='invalid application grammar'){
   invalid=await c.encrypt(new TextEncoder().encode('{"v":1,"id":1,"op":"write"}'));
   valid=await c.encrypt(encoded());
  }else{
   valid=await c.encrypt(encoded());
   invalid=failure==='invalid frame length'?Uint8Array.of(0):valid.slice();
   if(failure==='bad GCM tag')invalid[invalid.length-1]^=1;
  }
  let r=await h.call('info',invalid);denied(r);assert.equal(r.stopping,false);
  assert.equal(r.authenticated,false);
  // The exact review ordering: no poll/teardown between failure and valid frame.
  denied(await h.call('info',valid));
  denied(await h.call('raw-info',encoded()));
  denied(await h.call('encrypt',encoded()));
  denied(await h.call('session',Uint8Array.of(1)));
  assert.equal((await h.call('close')).error,0);
  denied(await h.call('new'));
  denied(await h.call('info',valid));
  assert.equal((await h.call('window')).error,0);c.destroy();c=await authenticate(h);
  r=await h.call('info',await c.encrypt(encoded()));assert.equal(r.error,0);
  const info=validateInfo(JSON.parse(new TextDecoder().decode(await c.decrypt(raw(r.data)))),1);
  assert.equal(info.synthetic,true);assert.equal(info.backups_paused,true);
 }finally{c.destroy();await h.close();}
});
test(guided?'valid encrypted reply survives normal close and a separately opened new setup window':'valid encrypted reply and normal disconnect/reconnect remain available in an unrejected window',async()=>{
 const h=host();let c=await authenticate(h);
 try{
  let r=await h.call('info',await c.encrypt(encoded()));assert.equal(r.error,0);
  validateInfo(JSON.parse(new TextDecoder().decode(await c.decrypt(raw(r.data)))),1);
  assert.equal(r.stopRequested,false);assert.equal(r.authenticated,true);
  assert.equal((await h.call('close')).error,0);
  if(guided){denied(await h.call('new'));assert.equal((await h.call('window')).error,0);}
  else assert.equal((await h.call('new')).error,0);
  c.destroy();c=await authenticate(h);
  r=await h.call('info',await c.encrypt(encoded()));assert.equal(r.error,0);
  validateInfo(JSON.parse(new TextDecoder().decode(await c.decrypt(raw(r.data)))),1);
  assert.equal(r.stopRequested,false);
 }finally{c.destroy();await h.close();}
});
test('valid GCM replayed application request latches denial before a later legitimate request',async()=>{
 const h=host(),c=await authenticate(h);
 try{
  let r=await h.call('info',await c.encrypt(encoded()));assert.equal(r.error,0);
  await c.decrypt(raw(r.data));
  denied(await h.call('info',await c.encrypt(encoded())));
  denied(await h.call('info',await c.encrypt(encoded(2))));
 }finally{c.destroy();await h.close();}
});
