import {test} from 'node:test';
import assert from 'node:assert/strict';
import {fixture,rows,FileHistoryClient,until} from './fixture.mjs';
async function session(options,action){const f=fixture(options);try{await f.connection.connect();await action(f,new FileHistoryClient(f.connection));}finally{f.close();}}
test('real ManagementConnection admits history only after advertised capability and private authentication',async()=>session({},async(f,client)=>{
 assert.equal((await f.connection.refresh()).file_history,true);const data=await client.read();
 assert.deepEqual(data.rows,rows.map(row=>({...row,path_bytes:new TextEncoder().encode(row.path).length})));assert(f.connected);
 assert.equal(f.requests.filter(r=>r.op==='history_row').length,6);assert.equal(f.requests[0].op,'history_status');
}));
for(const capability of [null,false])test('old/default-off firmware '+capability+' is refused without GATT history command',async()=>session({capability},async(f,client)=>{
 assert.notEqual((await f.connection.refresh()).file_history,true);await assert.rejects(client.read(),/Unsupported/);assert.deepEqual(f.requests,[]);assert.equal((await f.flow.status()).revision,'6');
}));
for(const [name,change] of [['numeric',{file_history:1}],['text',{file_history:'true'}],['missing version',{file_history_protocol:undefined}],['future version',{file_history_protocol:2}],['false with version',{file_history:false,file_history_protocol:1}]])test('invalid history capability '+name+' fails before authentication',async()=>{
 const f=fixture({metadata:meta=>{Object.assign(meta.csp,change);return meta;}});try{await assert.rejects(f.connection.connect());assert(!f.events.includes('session:write'));assert.deepEqual(f.requests,[]);}finally{f.close();}
});
test('Info cannot inject a file capability',async()=>{const f=fixture({info:{file_history:true}});try{await assert.rejects(f.connection.connect());assert.deepEqual(f.requests,[]);}finally{f.close();}});
test('failed authentication and unauthenticated reads admit no history',async()=>{
 for(const failAuthentication of [false,true]){const f=fixture({failAuthentication});try{if(failAuthentication)await assert.rejects(f.connection.connect());await assert.rejects(new FileHistoryClient(f.connection).read());assert.deepEqual(f.requests,[]);}finally{f.close();}}
});
test('reconnect to an older device drops the previous capability',async()=>session({},async(f,client)=>{
 await client.read();f.connection.disconnect();f.setMetadata(meta=>{delete meta.csp.file_history;delete meta.csp.file_history_protocol;return meta;});await f.connection.connect();const n=f.requests.length;await assert.rejects(client.read(),/Unsupported/);assert.equal(f.requests.length,n);
}));
test('eight paths of 240 UTF8 bytes are fetched in at most 33 bounded operations',async()=>{
 const list=Array.from({length:8},(_,i)=>({...rows[1],path:'QA/'+String(i).repeat(237)}));await session({rows:list},async(f,client)=>{assert.equal((await client.read()).rows.length,8);assert.equal(f.requests.length,33);});
});
test('no snapshot and explicit allocation failure are honest empty states',async()=>{
 for(const status of [{loaded:false},{allocation_failed:true,complete:false,limited:true},{exhausted:true}])await session({rows:[],status},async(f,client)=>{assert.deepEqual((await client.read()).rows,[]);assert.equal(f.requests.length,1);});
});
for(const field of ['ok','g','n','matched','when','loaded','complete','limited','corrupt','io_failed','allocation_failed','exhausted'])test('missing snapshot '+field+' closes session',async()=>session({status:{[field]:undefined}},async(f,client)=>{await assert.rejects(client.read());assert(!f.connected);}));
for(const change of [{g:-1},{g:1.1},{g:'3'},{g:0x100000000},{n:9},{n:-1},{matched:129},{matched:1},{when:'18446744073709551616'},{when:'01'},{when:1},{loaded:1},{allocation_failed:true},{exhausted:true},{loaded:false},{filename:'PRIVATE_MUST_NOT_REFLECT'}])test('bad snapshot '+JSON.stringify(change)+' never reflects private data',async()=>session({status:change},async(f,client)=>{await assert.rejects(client.read(),e=>!e.message.includes('PRIVATE_MUST_NOT_REFLECT'));assert(!f.connected);}));
for(const field of ['ok','g','i','n','b','s','v','r','e','a','p','o','x','m','h'])test('missing row '+field+' rejects',async()=>session({fragment:row=>({...row,[field]:undefined})},async(f,client)=>{await assert.rejects(client.read());assert(!f.connected);}));
for(const change of [{g:4},{i:1},{n:4},{b:'-1'},{b:'18446744073709551616'},{v:'01'},{r:1},{s:3},{s:-1},{e:-0x80000001},{e:0x80000000},{a:-1},{a:0x100000000},{p:241},{p:0},{o:1},{x:65},{x:-1},{m:'false'},{h:'FF'},{h:'0'},{h:'gg'},{name:'PRIVATE_MUST_NOT_REFLECT'}])test('bad row '+JSON.stringify(change)+' rejects',async()=>session({fragment:row=>({...row,...change})},async(f,client)=>{await assert.rejects(client.read(),e=>!e.message.includes('PRIVATE_MUST_NOT_REFLECT'));assert(!f.connected);}));
for(const field of ['b','s','v','r','e','a','p'])test('metadata changes between path fragments '+field+' cannot mix saved versions',async()=>session({rows:[rows[1]],fragment:(row,r)=>r.o?{...row,[field]:field==='p'?row.p-1:typeof row[field]==='string'?'1':row[field]+1}:row},async(f,client)=>{await assert.rejects(client.read());assert(!f.connected);}));
for(const path of ['../bad.pdf','QA/../bad.pdf','/absolute.pdf','QA//bad.pdf','QA/with\\slash.pdf','QA/with:colon.pdf','QA/hidden\0.pdf','QA/'+('x'.repeat(238))])test('invalid private path bytes reject '+path.length,async()=>session({rows:[{...rows[0],path}]},async(f,client)=>{await assert.rejects(client.read());assert(!f.connected);}));
test('invalid UTF8 never becomes replacement-character display',async()=>session({rows:[{...rows[0],path:'QA/x'}],fragment:row=>({...row,h:'51412fff'})},async(f,client)=>{await assert.rejects(client.read());assert(!f.connected);}));
for(const status of [true,false])test('late '+(status?'status':'fragment')+' response after disconnect cannot complete',async()=>session({delayStatus:status,delayOffset:status?undefined:64,rows:[rows[1]]},async(f,client)=>{
 const pending=client.read();await until(()=>f.waiting);f.connection.disconnect();f.release();await assert.rejects(pending);assert(!f.connected);
}));
