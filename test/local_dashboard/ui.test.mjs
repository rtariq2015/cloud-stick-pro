import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source=readFileSync(new URL('../../src/local_dashboard_assets.cpp',import.meta.url),'utf8');
const script=source.match(/R"JS\(([\s\S]*?)\)JS"/)[1];
const page=source.match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const tick=()=>new Promise(resolve=>setImmediate(resolve));
const fixture=overrides=>({firmware:'0.4.0-dashboard1',uptime_ms:40000,wifi:true,media:true,active:false,scanning:false,has_scan:true,scan_complete:true,window_open:true,paused:false,cloud:true,configured:true,verified:4,pending:0,failed:0,done:0,total:0,bytes_per_second:0,quiet_seconds:0,last_backup_unix:1793845305,error:0,percent:-1,phase:0,...overrides});
function environment(){
 const elements=new Map([...page.matchAll(/id="([^"]+)"/g)].map(([,name])=>[name,{textContent:'',value:undefined,removeAttribute(name){delete this[name];}}]));
 const documentListeners={},windowListeners={},requests=[],timers=new Map();let id=0;
 const document={hidden:false,getElementById:name=>{assert.ok(elements.has(name),name);return elements.get(name);},addEventListener:(name,fn)=>documentListeners[name]=fn};
 const window={addEventListener:(name,fn)=>windowListeners[name]=fn};
 const context=vm.createContext({document,window,AbortController,Date,Number,String,Math,Error,
  setTimeout:(fn,ms)=>{timers.set(++id,{fn,ms});return id;},clearTimeout:id=>timers.delete(id),
  fetch:(url,options)=>new Promise((resolve,reject)=>requests.push({url,options,resolve,reject}))});
 vm.runInContext(script,context);
 return {elements,requests,timers,document,windowListeners,documentListeners,
  text:id=>elements.get(id).textContent,
  result:async(state,index=requests.length-1)=>{requests[index].resolve({ok:true,json:async()=>state});await tick();},
  refresh:()=>{const next=[...timers].find(([,v])=>v.ms===3000);assert.ok(next);timers.delete(next[0]);next[1].fn();}};
}
test('static shell exposes no secret inputs, dependencies, file names or HTTP mutation',()=>{
 assert.match(page,/<title>Cloud Stick Pro/);assert.match(page,/Open Cloud Stick Pro setup/);
 assert.doesNotMatch(page,/<input|<form|<iframe/);assert.doesNotMatch(script,/localStorage|sessionStorage|currentFile|authorization|password\s*:/i);
 assert.equal([...script.matchAll(/fetch\(/g)].length,1);assert.match(script,/fetch\('\/status'/);assert.match(script,/credentials:'omit'/);
});
test('actual script displays scan counts and does not confuse local Wi-Fi with cloud verification',async()=>{
 const env=environment();assert.equal(env.requests[0].url,'/status');await env.result(fixture({cloud:false}));
 assert.equal(env.text('verified'),'4');assert.match(env.text('headline'),/verified/);assert.equal(env.text('wifi'),'Connected');
 assert.doesNotMatch(env.text('detail'),/internet.*connected|Koofr.*connected/i);
 env.refresh();await env.result(fixture({has_scan:false,verified:99}));assert.equal(env.text('verified'),'—');
});
test('actual transfer/time/error/missing media states',async()=>{
 const env=environment();await env.result(fixture({active:true,phase:2,done:1048576,total:2097152,bytes_per_second:24552,percent:50}));
 assert.match(env.text('headline'),/Uploading/);assert.match(env.text('bytes'),/1.0 MiB \/ 2.0 MiB/);assert.equal(env.elements.get('progress').value,50);
 env.refresh();await env.result(fixture({error:-2303}));assert.match(env.text('headline'),/network time/);
 env.refresh();await env.result(fixture({media:false}));assert.match(env.text('headline'),/microSD/);
 env.refresh();await env.result(fixture({active:true,scan_complete:false,scanning:true}));assert.match(env.text('scan'),/provisional/);
 env.refresh();await env.result(fixture({configured:false}));assert.match(env.text('headline'),/Cloud setup needed/);
 env.refresh();await env.result(fixture({active:true,phase:0}));assert.match(env.text('headline'),/Preparing the next backup/);
 env.refresh();await env.result(fixture({quiet_seconds:120}));assert.match(env.text('detail'),/120 seconds/);
});
test('idle status never replays retained readback phase, rate, progress or scanning flag',async()=>{
 const env=environment();await env.result(fixture({active:false,scanning:true,phase:3,done:3746,total:3746,bytes_per_second:49341,percent:90}));
 assert.equal(env.text('headline'),'Your files are verified');assert.equal(env.text('action'),'No transfer running');
 assert.equal(env.text('bytes'),'No transfer running');assert.equal(env.text('speed'),'—');assert.equal(env.elements.get('progress').value,100);
 assert.doesNotMatch(env.text('scan'),/in progress/);assert.match(env.text('last'),/^Last recorded verified backup:/);
 assert.equal(env.requests.length,1);assert.equal([...env.timers.values()].filter(t=>t.ms===3000).length,1);
});
test('incomplete inactive scans are partial records, not endless current scans',async()=>{
 const env=environment();await env.result(fixture({scan_complete:false,scanning:true,phase:3,done:3746,bytes_per_second:49341,percent:90}));
 assert.equal(env.text('headline'),'Waiting to continue');assert.match(env.text('scan'),/Partial counts.*no scan is running/);
 assert.equal(env.text('bytes'),'No transfer running');assert.equal(env.text('speed'),'—');
 assert.equal(env.elements.get('progress').hidden,true);assert.equal(env.elements.get('progress').value,undefined);
 env.refresh();await env.result(fixture({scan_complete:false,paused:true}));
 assert.equal(env.text('headline'),'Backups paused');assert.equal(env.text('action'),'Backup paused');assert.equal(env.text('bytes'),'Paused');
});
test('finding files, local hashing, upload, readback and publication remain distinct',async()=>{
 const env=environment();await env.result(fixture({active:true,scanning:true,scan_complete:false}));
 assert.equal(env.text('headline'),'Finding new or changed files');assert.doesNotMatch(env.text('detail'),/cloud connection|cloud access/);
 assert.equal(env.text('bytes'),'Comparing file records');assert.equal(env.text('speed'),'—');assert.equal(env.elements.get('progress').hidden,false);
 env.refresh();await env.result(fixture({active:true,phase:1,percent:50}));
 assert.equal(env.text('headline'),'Checking a USB file');assert.match(env.text('detail'),/local file.*not another upload/);
 env.refresh();await env.result(fixture({active:true,phase:2,percent:50}));assert.equal(env.text('headline'),'Uploading to Koofr');
 env.refresh();await env.result(fixture({active:true,phase:3,percent:50}));
 assert.equal(env.text('headline'),'Verifying the cloud copy');assert.match(env.text('detail'),/Reading back the cloud copy/);
 env.refresh();await env.result(fixture({active:true,phase:4,percent:100,bytes_per_second:999999}));
 assert.equal(env.text('headline'),'Publishing verified copy');assert.equal(env.elements.get('progress').value,undefined);assert.equal(env.text('speed'),'—');
});
test('failed or scheduled work never displays an indeterminate idle spinner as a transfer',async()=>{
 const env=environment();await env.result(fixture({scan_complete:false,pending:2}));
 assert.equal(env.text('headline'),'Waiting to retry backup');assert.equal(env.elements.get('progress').hidden,true);
 env.refresh();await env.result(fixture({window_open:false}));
 assert.equal(env.text('headline'),'Waiting for your schedule');assert.equal(env.elements.get('progress').hidden,true);
 env.refresh();await env.result(fixture({quiet_seconds:120}));
 assert.equal(env.text('headline'),'Waiting for file activity to settle');assert.equal(env.elements.get('progress').hidden,true);
 env.refresh();await env.result(fixture({error:401}));assert.equal(env.elements.get('progress').hidden,true);
 env.refresh();env.requests.at(-1).reject(Error('offline'));await tick();assert.equal(env.elements.get('progress').hidden,true);
});
test('failed fetch removes live success/progress/speed and explicitly marks historical counts',async()=>{
 const env=environment();await env.result(fixture());env.refresh();env.requests.at(-1).reject(Error('offline'));await tick();
 assert.match(env.text('live'),/unavailable/);assert.doesNotMatch(env.text('headline'),/verified/);
 assert.match(env.text('scan'),/Previous counts/);assert.equal(env.text('speed'),'—');assert.equal(env.elements.get('progress').value,undefined);
});
test('hide/restore revokes buffered JSON from the previous request',async()=>{
 const env=environment();let finish;
 env.requests[0].resolve({ok:true,json:()=>new Promise(resolve=>finish=resolve)});await tick();
 env.document.hidden=true;env.documentListeners.visibilitychange();assert.equal(env.requests[0].options.signal.aborted,true);
 env.document.hidden=false;env.documentListeners.visibilitychange();assert.equal(env.requests.length,2);
 finish(fixture({verified:999}));await tick();assert.notEqual(env.text('verified'),'999');assert.doesNotMatch(env.text('live'),/^Live/);
 await env.result(fixture({verified:5}),1);assert.equal(env.text('verified'),'5');assert.match(env.text('live'),/^Live/);
});
test('pagehide/BFCache restoration also rejects late responses and creates one fresh request',async()=>{
 const env=environment();env.windowListeners.pagehide();assert.equal(env.requests[0].options.signal.aborted,true);
 env.windowListeners.pageshow({persisted:true});assert.equal(env.requests.length,2);
 await env.result(fixture({verified:999}),0);assert.notEqual(env.text('verified'),'999');
 await env.result(fixture({verified:6}),1);assert.equal(env.text('verified'),'6');
 assert.equal([...env.timers.values()].filter(t=>t.ms===3000).length,1);
});
