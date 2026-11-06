import {test} from 'node:test';
import assert from 'node:assert/strict';
import {fixture,events,DiagnosticLogClient,until} from './fixture.mjs';
async function session(options,action){const f=fixture(options);try{await f.connection.connect();await action(f,new DiagnosticLogClient(f.connection));}finally{f.close();}}
test('actual connection admits private numeric log pages only after Security2 and preserves all facts',async()=>session({},async(f,client)=>{
 const info=await f.connection.refresh();assert.equal(info.event_log,true);const log=await client.read();assert.deepEqual(log.events,events);
 assert.deepEqual(f.requests.map(({op,cursor})=>({op,cursor})),[{op:'event_log',cursor:0},{op:'event_log',cursor:21},{op:'event_log',cursor:22}]);
 assert.equal(log.newest,23);assert.equal(log.persisted,23);assert.equal(log.loaded,true);assert(f.connected);
}));
for(const capability of [null,false])test('default/disabled capability '+capability+' rejects before a GATT command',async()=>session({capability},async(f,client)=>{
 assert.notEqual((await f.connection.refresh()).event_log,true);await assert.rejects(client.read(),/Unsupported/);assert.deepEqual(f.requests,[]);
 assert.equal((await f.flow.status()).revision,'6');
}));
for(const [name,change] of [
 ['numeric capability',{event_log:1}],['string capability',{event_log:'true'}],['null capability',{event_log:null}],
 ['missing version',{event_log_protocol:undefined}],['string version',{event_log_protocol:'1'}],['future version',{event_log_protocol:2}],
 ['false with version',{event_log:false,event_log_protocol:1}],
])test('metadata rejects '+name+' before authentication/application writes',async()=>{
 const f=fixture({metadata:meta=>{Object.assign(meta.csp,change);return meta;}});try{await assert.rejects(f.connection.connect());assert(!f.events.includes('session:write'));assert.deepEqual(f.requests,[]);}finally{f.close();}
});
test('wire Info cannot inject an event-log capability',async()=>{const f=fixture({info:{event_log:true}});try{await assert.rejects(f.connection.connect());assert.deepEqual(f.requests,[]);}finally{f.close();}});
test('failed Security2 never admits a log command',async()=>{const f=fixture({failAuthentication:true});try{await assert.rejects(f.connection.connect());await assert.rejects(new DiagnosticLogClient(f.connection).read());assert.deepEqual(f.requests,[]);}finally{f.close();}});
test('unauthenticated log read cannot reach GATT',async()=>{const f=fixture();try{await assert.rejects(new DiagnosticLogClient(f.connection).read(),/Connect/);assert.deepEqual(f.requests,[]);}finally{f.close();}});
test('disconnect/reconnect clears capability before an older device is used',async()=>session({},async(f,client)=>{
 await client.read();f.connection.disconnect();f.setMetadata(meta=>{delete meta.csp.event_log;delete meta.csp.event_log_protocol;return meta;});await f.connection.connect();
 const count=f.requests.length;await assert.rejects(client.read(),/Unsupported/);assert.equal(f.requests.length,count);
}));
for(const reserved of [{v:2},{id:99},{op:'status'}])test('reserved '+Object.keys(reserved)[0]+' cannot rewrite a log request',async()=>session({},async f=>{
 await assert.rejects(f.connection.command('event_log',{cursor:0,...reserved}),/Unsupported/);assert.deepEqual(f.requests,[]);
}));
test('twelve retained events may start beyond sequence one without skipping later records',async()=>{
 const rows=Array.from({length:12},(_,i)=>({s:i+100,u:i,t:0,k:6,c:0}));await session({events:rows},async(f,client)=>{
  assert.deepEqual((await client.read()).events,rows);assert.equal(f.requests.length,12);assert.equal(f.requests.at(-1).cursor,110);
 });
});
test('thirteenth retained event is rejected without requesting an unbounded extra page',async()=>session({events:Array.from({length:13},(_,i)=>({s:i+1,u:i,t:0,k:6,c:0}))},async(f,client)=>{
 await assert.rejects(client.read(),/activity changed or was invalid/);assert.equal(f.requests.length,12);assert(!f.connected);
}));
test('empty history is valid only at cursor/newest zero',async()=>session({events:[]},async(f,client)=>{assert.deepEqual((await client.read()).events,[]);assert.deepEqual(f.requests.map(r=>r.cursor),[0]);}));
test('maximum sequence/uptime/time and signed minimum code are lossless',async()=>session({events:[{s:0xffffffff,u:0xffffffff,t:253402300799,k:8,c:-0x80000000}]},async(f,client)=>{
 const log=await client.read();assert.equal(log.events[0].s,0xffffffff);assert.equal(log.events[0].c,-0x80000000);
}));
for(const field of ['ok','events','next','more','newest','persisted','loaded','dirty','corrupt','io_failed','exhausted'])test('missing '+field+' rejects and disconnects',async()=>session({page:page=>({...page,[field]:undefined})},async(f,client)=>{
 await assert.rejects(client.read());assert(!f.connected);
}));
for(const [name,change] of [
 ['unknown filename',{filename:'PRIVATE_MUST_NOT_REFLECT.pdf'}],['ok number',{ok:1}],['negative cursor',{next:-1}],['fraction cursor',{next:1.1}],
 ['cursor overflow',{next:0x100000000}],['newest text',{newest:'23'}],['negative newest',{newest:-1}],['fraction newest',{newest:23.1}],
 ['newest overflow',{newest:0x100000000}],['persisted text',{persisted:'0'}],['persisted negative',{persisted:-1}],['persisted overflow',{persisted:0x100000000}],
 ['persisted beyond newest',{persisted:24}],['more text',{more:'true'}],['events null',{events:null}],['events object',{events:{}}],['events text',{events:'PRIVATE_MUST_NOT_REFLECT'}],
 ['two events',{events:[events[0],events[1]]}],['empty loop',{events:[],next:0,more:true}],['empty before newest',{events:[],next:0,more:false}],
 ['wrong next',{next:22}],['early end',{more:false}],
])test('malformed page '+name+' is not accepted',async()=>session({page:page=>({...page,...change})},async(f,client)=>{
 await assert.rejects(client.read(),error=>!error.message.includes('PRIVATE_MUST_NOT_REFLECT'));assert(!f.connected);
}));
for(const flag of ['loaded','dirty','corrupt','io_failed','exhausted'])test('nonboolean '+flag+' rejects',async()=>session({page:page=>({...page,[flag]:1})},async(f,client)=>{await assert.rejects(client.read());assert(!f.connected);}));
for(const field of ['s','u','t','k','c'])test('missing event '+field+' rejects',async()=>session({page:page=>({...page,events:[{...page.events[0],[field]:undefined}]})},async(f,client)=>{await assert.rejects(client.read());assert(!f.connected);}));
for(const [name,change] of [
 ['unknown event text',{name:'PRIVATE_MUST_NOT_REFLECT'}],['bytes not in wire protocol',{bytes:1}],['zero sequence',{s:0}],['sequence text',{s:'21'}],
 ['sequence negative',{s:-1}],['sequence fraction',{s:21.1}],['sequence overflow',{s:0x100000000}],['sequence beyond newest',{s:24}],
 ['uptime text',{u:'1'}],['uptime negative',{u:-1}],['uptime fraction',{u:1.1}],['uptime overflow',{u:0x100000000}],
 ['time text',{t:'0'}],['time negative',{t:-1}],['time fraction',{t:1793793600.1}],['time before accepted clock',{t:1767225599}],['time after accepted clock',{t:253402300800}],
 ['zero kind',{k:0}],['unknown kind',{k:9}],['kind text',{k:'6'}],['kind fraction',{k:1.1}],
 ['low code',{c:-0x80000001}],['high code',{c:0x80000000}],['code text',{c:'0'}],['code fraction',{c:1.1}],
 ['negative verified count',{k:4,c:-1}],['overflow verified count',{k:4,c:129}],
])test('malformed event '+name+' is not reflected',async()=>session({page:page=>({...page,events:[{...page.events[0],...change}]})},async(f,client)=>{
 await assert.rejects(client.read(),error=>!error.message.includes('PRIVATE_MUST_NOT_REFLECT'));assert(!f.connected);
}));
for(const field of ['newest','persisted','loaded','dirty','corrupt','io_failed','exhausted'])test('changed snapshot '+field+' between pages rejects mixing history',async()=>session({page:(page,r)=>r.cursor?{...page,[field]:typeof page[field]==='boolean'?!page[field]:page[field]+(field==='persisted'?-1:1)}:page},async(f,client)=>{
 await assert.rejects(client.read());assert(!f.connected);assert.equal(f.requests.length,2);
}));
for(const change of [{s:21},{s:20},{s:23}])test('duplicate/backward/skipped sequence '+change.s+' is rejected',async()=>session({page:(page,r)=>r.cursor?{...page,events:[{...page.events[0],...change}],next:change.s}:page},async(f,client)=>{
 await assert.rejects(client.read());assert(!f.connected);
}));
test('a second page cannot claim more after reaching newest',async()=>session({page:(page,r)=>r.cursor?{...page,events:[{...page.events[0],s:23}],next:23,more:true}:page},async(f,client)=>{await assert.rejects(client.read());assert(!f.connected);}));
test('delayed second page cannot complete after real connection teardown',async()=>session({delayCursor:21},async(f,client)=>{
 const pending=client.read();await until(()=>f.waiting);assert.equal(f.requests.length,2);f.connection.disconnect();f.release();await assert.rejects(pending,/Connection was closed/);assert(!f.connected);
}));
test('one maximally sized numeric page stays under the existing 400-byte backend body budget',async()=>session({events:[{s:0xffffffff,u:0xffffffff,t:253402300799,k:8,c:-0x80000000}],status:{loaded:false,dirty:true,corrupt:true,io_failed:true,exhausted:true},page:page=>{
 assert(new TextEncoder().encode(JSON.stringify(page)).length<=400);return page;
}},async(f,client)=>{assert.equal((await client.read()).events.length,1);}));
