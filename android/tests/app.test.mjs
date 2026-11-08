import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createViewer,readableSize,viewerHelp,backupAttention} from '../app/src/main/assets/app.mjs';
class Element {
 value='';hidden=false;disabled=false;textContent='';children=[];listeners=new Map();
 addEventListener(name,fn){this.listeners.set(name,fn);} append(...nodes){this.children.push(...nodes);}replaceChildren(...nodes){this.children=[...nodes];}
 async fire(name){return this.listeners.get(name)?.({preventDefault(){}});}
}
function dom(){const elements=new Map(),listeners=new Map();return {hidden:false,getElementById(id){if(!elements.has(id))elements.set(id,new Element());return elements.get(id);},createElement(){return new Element();},addEventListener(name,fn){listeners.set(name,fn);},fire(name){listeners.get(name)?.();}};}
const tick=async()=>{for(let i=0;i<30;i++)await Promise.resolve();};
function fixture({permission=async()=>({}),connect,refresh,close=async()=>{},status={}}={}){
 const document=dom(),$=id=>document.getElementById(id),calls=[];let options,connected=0,disconnected=0;
 const info={firmware:'0.4.1-setup-dev',wifi_connected:true,backup_status:true,file_history:true};
 const client={async connect(){connected++;return connect?connect():info;},async refresh(){return refresh?refresh():info;},disconnect(){disconnected++;options?.onState({state:'disconnected'});},async command(op){calls.push(op);if(op==='backup_status')return {ok:true,cloud_configured:true,last_backup_unix:123,verified:10,pending:0,failed:0,backup_error:0,backups_paused:true,...status};if(op==='history_status')return {ok:true,g:1,n:0,matched:0,when:'123',loaded:true,complete:true,limited:false,corrupt:false,io_failed:false,allocation_failed:false,exhausted:false};throw Error('unexpected write command');}};
 const rpc={onEvent(){},event(){},request:async op=>{assert.equal(op,'permission');return permission();}};
 createViewer({document,rpc,bluetoothFactory:()=>({close,drain:async()=>{}}),connectionFactory:value=>{options=value;return client;}});
 $('username').value='synthetic-owner';$('password').value='PUBLIC-SYNTHETIC-PASSWORD';
 return {document,$,calls,client,get options(){return options;},get connected(){return connected;},get disconnected(){return disconnected;}};
}
test('actual viewer grants permission before creating any authenticated session',async()=>{
 let grant;const f=fixture({permission:()=>new Promise(resolve=>{grant=resolve;})});const pending=f.$('login-form').fire('submit');await tick();assert.equal(f.options,undefined);assert.equal(f.$('password').value,'');
 // Native permission dialog backgrounds/resumes Android before the grant reply.
 f.document.hidden=true;f.document.fire('visibilitychange');globalThis.cloudStickNativeEvent({type:'disconnected'});f.document.hidden=false;grant({});await pending;
 assert.equal(f.connected,1);assert.equal(f.$('result').hidden,false);assert.deepEqual(f.calls,['backup_status','history_status']);assert.equal(f.options.ownerKey,undefined);assert.equal(f.options.sessionTimeoutMs,null);
});
test('terminal client event synchronously clears results without waiting native disconnect',async()=>{
 const f=fixture();await f.$('login-form').fire('submit');assert.equal(f.$('result').hidden,false);f.$('history').append(new Element());
 f.options.onState({state:'disconnected',message:'timer expired'});
 assert.equal(f.$('result').hidden,true);assert.equal(f.$('status').children.length,0);assert.equal(f.$('history').children.length,0);assert.equal(f.$('password').value,'');assert.equal(f.$('login').hidden,false);assert.equal(f.disconnected,1);
});
test('terminal event rejects late refresh from restoring old private results',async()=>{
 let resolve,late=false;const f=fixture({refresh:async()=>late?new Promise(r=>{resolve=r;}):{firmware:'synthetic',backup_status:true,file_history:true,wifi_connected:true}});await f.$('login-form').fire('submit');late=true;
 const pending=f.$('refresh').fire('click');await tick();f.options.onState({state:'error'});resolve({firmware:'stale',backup_status:true,file_history:true,wifi_connected:true});await pending;
 assert.equal(f.$('result').hidden,true);assert.equal(f.$('status').children.length,0);assert.deepEqual(f.calls,['backup_status','history_status']);
});
test('background and Done clear passwords/results and close session',async()=>{
 const f=fixture();await f.$('login-form').fire('submit');f.document.hidden=true;f.document.fire('visibilitychange');assert.equal(f.disconnected,1);assert.equal(f.$('result').hidden,true);await f.$('done').fire('click');assert.equal(f.disconnected,1);assert.equal(f.$('password').value,'');
});
test('initial status reuses validated connect info; explicit Refresh alone reads info again',async()=>{
 let reads=0;const f=fixture({refresh:async()=>{reads++;return {firmware:'synthetic',backup_status:true,file_history:true,wifi_connected:true};}});await f.$('login-form').fire('submit');assert.equal(reads,0);await f.$('refresh').fire('click');assert.equal(reads,1);assert.deepEqual(f.calls,['backup_status','history_status','backup_status','history_status']);
});
test('Done clears private snapshot immediately and waits native ACK before claiming link closed',async()=>{
 let ack;const f=fixture({close:()=>new Promise(resolve=>{ack=resolve;})});await f.$('login-form').fire('submit');const pending=f.$('done').fire('click');await tick();assert.equal(f.$('result').hidden,true);assert.equal(f.$('count-verified').textContent,'—');assert.equal(f.$('connect').disabled,true);assert.equal(f.$('connection-badge').textContent,'Closing Bluetooth');ack({});await pending;assert.equal(f.$('connect').disabled,false);assert.match(f.$('message').textContent,/confirmed its Bluetooth link closed/);assert.match(f.$('message').textContent,/does not confirm that backups have resumed/);
});
test('native close failure does not claim link closure or worker resumption',async()=>{
 const f=fixture({close:async()=>{throw Error('PRIVATE-OS-DETAIL');}});await f.$('login-form').fire('submit');await f.$('done').fire('click');assert.equal(f.$('connection-badge').textContent,'Close not confirmed');assert.equal(f.$('connect').disabled,true);await f.$('login-form').fire('submit');assert.equal(f.connected,1);assert.match(f.$('message').textContent,/could not confirm/);assert.doesNotMatch(f.$('message').textContent,/PRIVATE-OS-DETAIL/);
});
test('summary keeps recorded counts separate from snapshot read time and empty history',async()=>{
 const f=fixture({status:{verified:10,pending:2,failed:1}});await f.$('login-form').fire('submit');assert.equal(f.$('count-verified').textContent,'10');assert.equal(f.$('count-pending').textContent,'2');assert.equal(f.$('count-failed').textContent,'1');assert.match(f.$('snapshot-read').textContent,/recorded earlier.*did not test the cloud/);assert.match(f.$('card-failed').className,/tone-error/);assert.match(f.$('history').children[0].textContent,/No saved file records/);assert.match(f.$('backup-attention').textContent,/not verified/);
});
test('sizes remain exact for large integers and fixed error help never echoes arbitrary details',()=>{
 assert.equal(readableSize('2097152'),'2 MiB');assert.match(readableSize('18446744073709551615'),/bytes/);assert.equal(readableSize('bad'),'Size unavailable');assert.match(viewerHelp({message:'bluetooth_permission_denied'}),/Nearby devices/);assert.match(viewerHelp({message:'SYNTHETIC-SECRET'},'authenticating'),/current personal username/);assert.doesNotMatch(viewerHelp({message:'SYNTHETIC-SECRET'}),/SYNTHETIC/);assert.match(backupAttention({backup_error:401}),/Koofr.*Device code: 401/);assert.doesNotMatch(backupAttention({backup_error:0,pending:0,failed:0}),/all.*verified|up to date/i);
});

test('initial connection error uses its fixed actionable help instead of racing generic cleanup',async()=>{
 let f;const error=Object.assign(new Error('PUBLIC-NOT-A-REAL-SECRET'),{name:'NotFoundError'});
 f=fixture({connect:async()=>{f.options.onState({state:'error',message:'PUBLIC-NOT-A-REAL-SECRET'});throw error;}});
 await f.$('login-form').fire('submit');await tick();assert.equal(f.$('result').hidden,true);assert.match(f.$('message').textContent,/No stick selected/);assert.doesNotMatch(f.$('message').textContent,/REAL-SECRET/);assert.equal(f.$('connection-badge').textContent,'Bluetooth closed');
});
test('initial authentication failure preserves login guidance and clears the password',async()=>{
 let f;f=fixture({connect:async()=>{f.options.onState({state:'authenticating'});f.options.onState({state:'error',message:'PUBLIC-SECRET'});throw Error('PUBLIC-SECRET');}});
 await f.$('login-form').fire('submit');await tick();assert.match(f.$('message').textContent,/current personal username and password/);assert.doesNotMatch(f.$('message').textContent,/PUBLIC-SECRET/);assert.equal(f.$('password').value,'');assert.equal(f.$('result').hidden,true);
});
