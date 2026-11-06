import {test} from 'node:test';
import assert from 'node:assert/strict';
import {fixture,prepared,handed,until} from './fixture.mjs';
const bind={profile:1,revision:'6'};
async function session(options={},action){const f=fixture(options);try{await f.connection.connect();await action(f);}finally{f.close();}}

test('actual dispatcher authenticates advertised protocol and binds both requests to the saved profile/revision/grant',async()=>session({},async f=>{
 const info=await f.connection.refresh();assert.equal(info.public_wifi,true);
 assert.deepEqual(await f.portal.prepare(bind),prepared);
 assert.deepEqual(await f.portal.handoff(),{ap_ssid:prepared.ap_ssid,ap_password:'admin123',window_seconds:300});
 assert.deepEqual(f.requests.map(({v,id,op,...args})=>({op,...args})),[
  {op:'portal_begin',...bind},{op:'portal_handoff',...bind,grant:'42'}]);
 assert(f.connected,'Client must not disconnect before caller reads the ACK');
 f.connection.disconnect();assert(f.events.indexOf('portal_handoff:read-complete')<f.events.lastIndexOf('gatt:disconnect'));
}));
test('pending handoff result is fully fetched before caller explicitly disconnects',async()=>session({pendingOp:'portal_handoff'},async f=>{
 await f.portal.prepare(bind);await f.portal.handoff();assert(f.connected);
 assert.deepEqual(f.requests.map(r=>r.op),['portal_begin','portal_handoff','result']);
 f.connection.disconnect();assert(f.events.indexOf('result:read-complete')<f.events.lastIndexOf('gatt:disconnect'));
}));
test('single-use grant cannot be submitted a second time',async()=>session({},async f=>{
 await f.portal.prepare(bind);await f.portal.handoff();await assert.rejects(f.portal.handoff(),/Prepare/);
 assert.equal(f.requests.filter(r=>r.op==='portal_handoff').length,1);
}));
test('maximum profile/revision/grant stay exactly bound as integer strings',async()=>session({reply:(op,r)=>op==='portal_begin'?{
 ...prepared,profile:r.profile,revision:r.revision,grant:'18446744073709551615'}:undefined},async f=>{
 const maximum={profile:0xffffffff,revision:'18446744073709551615'};await f.portal.prepare(maximum);await f.portal.handoff();
 assert.equal(f.requests.at(-1).profile,maximum.profile);assert.equal(f.requests.at(-1).revision,maximum.revision);assert.equal(f.requests.at(-1).grant,maximum.revision);
}));
test('uncertain negative handoff ACK consumes the local grant',async()=>session({reply:op=>op==='portal_handoff'?{ok:false,error:3}:undefined},async f=>{
 await f.portal.prepare(bind);await assert.rejects(f.portal.handoff());await assert.rejects(f.portal.handoff(),/Prepare/);
 assert.equal(f.requests.filter(r=>r.op==='portal_handoff').length,1);
}));
test('cancel submits only the grant and consumes local preparation',async()=>session({},async f=>{
 await f.portal.prepare(bind);await f.portal.cancel();await f.portal.cancel();await assert.rejects(f.portal.handoff(),/Prepare/);
 assert.deepEqual(f.requests.map(({op,grant})=>({op,grant})),[{op:'portal_begin',grant:undefined},{op:'portal_cancel',grant:'42'}]);
}));
for(const [name,change] of [
 ['unknown field',{unexpected:'PRIVATE_DO_NOT_REFLECT'}],['missing phase',{phase:undefined}],['wrong phase',{phase:'active'}],
 ['no cancellation code',{portal_error:0}],['text error',{portal_error:'6'}],['missing error',{portal_error:undefined}],
 ['nonzero remaining',{remaining_ms:1}],['text remaining',{remaining_ms:'0'}],['missing remaining',{remaining_ms:undefined}],
 ['false cloud success',{attempt_dav_read_verified:true}],['missing cloud flag',{attempt_dav_read_verified:undefined}],
])test('strict cancellation rejects '+name+' and disconnects',async()=>session({reply:op=>op==='portal_cancel'?{
 ok:true,phase:'complete',portal_error:6,remaining_ms:0,attempt_dav_read_verified:false,...change}:undefined},async f=>{
 await f.portal.prepare(bind);await assert.rejects(f.portal.cancel(),/Invalid sign-in handoff/);assert(!f.connected);
 await assert.rejects(f.portal.handoff(),/Prepare/);assert.equal(f.requests.filter(r=>r.op==='portal_cancel').length,1);
}));
for(const capability of [undefined,false])test('default/disabled public feature '+String(capability)+' rejects commands before application writes',async()=>session({metadata:meta=>{
 delete meta.csp.public_wifi_protocol;if(capability===undefined)delete meta.csp.public_wifi;else meta.csp.public_wifi=capability;return meta;
}},async f=>{
 assert.notEqual((await f.connection.refresh()).public_wifi,true);
 for(const op of ['portal_begin','portal_handoff','portal_cancel','portal_status'])await assert.rejects(f.connection.command(op,bind),/Unsupported/);
 assert.deepEqual(f.requests,[]);assert.equal((await f.flow.status()).revision,'6');
}));
for(const [name,change] of [
 ['text capability',{public_wifi:'true'}],['numeric capability',{public_wifi:1}],['null capability',{public_wifi:null}],
 ['missing version',{public_wifi:true,public_wifi_protocol:undefined}],['text version',{public_wifi_protocol:'1'}],
 ['future protocol',{public_wifi_protocol:2}],['false with version',{public_wifi:false,public_wifi_protocol:1}],
])test('metadata rejects '+name+' before Security2 and commands',async()=>{
 const f=fixture({metadata:meta=>{Object.assign(meta.csp,change);return meta;}});try{
  await assert.rejects(f.connection.connect());assert.equal(f.events.filter(e=>e==='session:write').length,0);assert.deepEqual(f.requests,[]);
 }finally{f.close();}
});
test('wire info cannot inject the local authenticated public capability',async()=>{
 const f=fixture({info:{public_wifi:true}});try{await assert.rejects(f.connection.connect(),/Invalid or unexpected/);assert.deepEqual(f.requests,[]);}finally{f.close();}
});
test('authentication failure never admits public commands',async()=>{
 const f=fixture({failAuthentication:true});try{await assert.rejects(f.connection.connect());await assert.rejects(f.portal.prepare(bind),/Connect/);assert.deepEqual(f.requests,[]);}finally{f.close();}
});
test('unauthenticated connection rejects public commands',async()=>{
 const f=fixture();try{await assert.rejects(f.portal.prepare(bind),/Connect/);assert.deepEqual(f.requests,[]);}finally{f.close();}
});
for(const [name,change] of [
 ['unknown envelope field',{unexpected:'PRIVATE_DO_NOT_REFLECT'}],['wrong response id',{id:999}],['wrong envelope version',{v:1}],
 ['zero job',{job:0}],['overflow job',{job:0x100000000}],['text pending',{pending:'false'}],
 ['array operation result',{data:[]}],['missing operation result',{data:undefined}],
])test('actual dispatcher rejects '+name+' and closes transport',async()=>session({envelope:e=>({...e,...change})},async f=>{
 await assert.rejects(f.portal.prepare(bind));assert(!f.connected);assert.equal(f.requests.length,1);
}));
test('actual dispatcher rejects a pending ACK fetched from a different job',async()=>session({pendingOp:'portal_handoff',envelope:(e,r)=>r.op==='result'?{...e,job:e.job+1}:e},async f=>{
 await f.portal.prepare(bind);await assert.rejects(f.portal.handoff(),/different operation/);assert(!f.connected);
}));
test('actual transport rejects oversized attribute before a preparation can be accepted',async()=>session({reply:op=>op==='portal_begin'?{...prepared,ap_ssid:'PRIVATE_DO_NOT_REFLECT'.repeat(40)}:undefined},async f=>{
 await assert.rejects(f.portal.prepare(bind),/too large/);assert(!f.connected);await assert.rejects(f.portal.handoff(),/Prepare/);
}));
test('capability is cleared across reconnect to a default device',async()=>session({},async f=>{
 f.connection.disconnect();f.setMetadata(meta=>{delete meta.csp.public_wifi;delete meta.csp.public_wifi_protocol;return meta;});
 await f.connection.connect();await assert.rejects(f.portal.prepare(bind),/Unsupported/);assert.deepEqual(f.requests,[]);
}));
for(const args of [{v:1},{id:9},{op:'status'}])test('reserved '+Object.keys(args)[0]+' cannot rewrite portal request envelope',async()=>session({},async f=>{
 await assert.rejects(f.connection.command('portal_begin',{...bind,...args}),/Unsupported/);assert.deepEqual(f.requests,[]);
}));
for(const [name,input] of [
 ['zero profile',{...bind,profile:0}],['overflow profile',{...bind,profile:0x100000000}],['fraction profile',{...bind,profile:1.2}],
 ['text profile',{...bind,profile:'1'}],['zero revision',{...bind,revision:'0'}],['leading zero revision',{...bind,revision:'06'}],
 ['overflow revision',{...bind,revision:'18446744073709551616'}],['numeric revision',{...bind,revision:6}],
])test('invalid binding '+name+' never reaches GATT',async()=>session({},async f=>{await assert.rejects(f.portal.prepare(input));assert.deepEqual(f.requests,[]);}));
for(const [name,change] of [
 ['unknown field',{unexpected:'PRIVATE_DO_NOT_REFLECT'}],['wrong profile',{profile:2}],['changed revision',{revision:'7'}],
 ['zero grant',{grant:'0'}],['numeric grant',{grant:42}],['leading zero grant',{grant:'042'}],['overflow grant',{grant:'18446744073709551616'}],
 ['wrong phase',{phase:'active'}],['SSID text',{ap_ssid:'PRIVATE_DO_NOT_REFLECT'}],['uppercase suffix',{ap_ssid:'CSP-Guest-ABCDEF'}],
 ['password mismatch',{ap_password:'PRIVATE_DO_NOT_REFLECT'}],['changed lifetime',{window_seconds:301}],['changed handoff',{handoff_ms:30001}],
 ['wrong next',{next:'disconnect_now'}],['missing grant',{grant:undefined}],
])test('strict preparation rejects '+name+' and clears transport/grant',async()=>session({reply:op=>op==='portal_begin'?{...prepared,...change}:undefined},async f=>{
 await assert.rejects(f.portal.prepare(bind),/Invalid sign-in handoff/);assert(!f.connected);await assert.rejects(f.portal.handoff(),/Prepare/);
 assert.equal(f.requests.length,1);
}));
for(const [name,change] of [
 ['unknown field',{unexpected:'PRIVATE_DO_NOT_REFLECT'}],['changed phase',{phase:'active'}],['zero remaining',{remaining_ms:0}],
 ['too long remaining',{remaining_ms:10001}],['fraction remaining',{remaining_ms:1.2}],['missing remaining',{remaining_ms:undefined}],
 ['wrong next',{next:'disconnect_now'}],['portal error',{portal_error:1}],['false cloud success',{attempt_dav_read_verified:true}],
])test('strict handoff rejects '+name+' without success instructions',async()=>session({reply:op=>op==='portal_handoff'?{...handed,...change}:undefined},async f=>{
 await f.portal.prepare(bind);await assert.rejects(f.portal.handoff(),/Invalid sign-in handoff/);assert(!f.connected);
 await assert.rejects(f.portal.handoff(),/Prepare/);assert.equal(f.requests.filter(r=>r.op==='portal_handoff').length,1);
}));
for(const phase of ['portal_begin','portal_handoff'])test('device rejects changed revision/profile during '+phase+' without an AP success claim',async()=>session({reply:op=>op===phase?{ok:false,error:3}:undefined},async f=>{
 if(phase==='portal_handoff')await f.portal.prepare(bind);
 await assert.rejects(phase==='portal_begin'?f.portal.prepare(bind):f.portal.handoff(),error=>error.code===3);
 assert(!f.requests.some(r=>r.op==='commit'));
}));
for(const phase of ['portal_begin','portal_handoff'])test('late '+phase+' response cannot survive connection teardown',async()=>session({delayOp:phase},async f=>{
 if(phase==='portal_handoff')await f.portal.prepare(bind);
 const pending=phase==='portal_begin'?f.portal.prepare(bind):f.portal.handoff();await until(()=>f.waiting);
 f.connection.disconnect();f.release();await assert.rejects(pending,/Connection was closed/);assert(!f.connected);
 await assert.rejects(f.portal.handoff());
}));
