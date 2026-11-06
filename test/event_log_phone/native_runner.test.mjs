// Runs the actual native runner body with an actual production connection/client
// stack on synthetic GATT. spawn/stdin/RPC are inert in-memory fixtures; Python,
// BlueZ, D-Bus, private files and physical Bluetooth are never invoked.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {fixture,DiagnosticLogClient,WifiSetupClient,PublicWifiClient} from './fixture.mjs';
import {SERVICE} from '../../phone/wifi-setup/connection.mjs';
const source=readFileSync(new URL('../../tools/native_ble/run.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/gm,'').replaceAll('import.meta.url',JSON.stringify(new URL('../../tools/native_ble/run.mjs',import.meta.url).href));
const KEY='PUBLIC_OWNER_KEY_'.padEnd(32,'X');
async function run(mode,options={}){
 const f=fixture(options),output=[],bridge=[],connections=[];let rpcLine;
 const stdin={privateFixture:true},handlers=new Map();
 const child={exitCode:null,signalCode:null,stderr:{on(){}},stdout:{},
  on:(name,fn)=>handlers.set(name,fn),once:(name,fn)=>handlers.set(name,fn),
  stdin:{destroyed:false,on(){},write(text){const request=JSON.parse(text);bridge.push(request.op);f.events.push('bridge:'+request.op);queueMicrotask(()=>rpcLine(JSON.stringify({id:request.id,ok:true})));},
   end(){child.exitCode=0;handlers.get('exit')?.(0);}}};
 const process={stdin,stdout:{write:text=>output.push(JSON.parse(text))},exitCode:0};
 const context={process,Buffer,URL,fileURLToPath,performance,setTimeout,clearTimeout,queueMicrotask,
  spawn:(program,args,config)=>{assert.equal(program,'/usr/bin/python3');assert(args[0].endsWith('/bluez_bridge.py'));assert.deepEqual([...config.stdio],['pipe','pipe','pipe']);return child;},
  createInterface:({input})=>input===stdin?{once:(name,fn)=>{assert.equal(name,'line');queueMicrotask(()=>fn(JSON.stringify({mode,address:'00:11:22:33:44:55',ownerKey:KEY})));},close(){}}:
   {on:(name,fn)=>{assert.equal(name,'line');rpcLine=fn;},close(){}},
  ManagementConnection:class{constructor(options){const connection=f.create({ownerKey:options.ownerKey,onState:options.onState});connections.push(connection);return connection;}},
  DiagnosticLogClient,WifiSetupClient,PublicWifiClient,SERVICE};
 try{await vm.runInNewContext('(async()=>{'+source+'\n})()',context);}finally{for(const connection of connections)connection.disconnect();f.close();}
 return {f,output,bridge,exit:process.exitCode};
}
function sanitized(result){
 const text=JSON.stringify(result.output);
 for(const secret of [KEY,'PRIVATE_SYNTHETIC_VENUE','CSP-Guest-a1b2c3','admin123','synthetic.invalid','https:','grant','ap_ssid','ap_password','ownerKey','filename'])assert(!text.includes(secret),secret);
 assert(!result.output.some(row=>Object.hasOwn(row,'firmware')));
}
test('native activity mode reads actual authenticated numeric pages without scan or configuration changes',async()=>{
 const r=await run('activity');assert.equal(r.exit,0);sanitized(r);
 const log=r.output.find(row=>row.event==='activity_log');assert(log);assert.equal(log.events.length,3);assert.equal(log.newest,23);
 assert.deepEqual(r.f.requests.map(request=>request.op),['status','event_log','event_log','event_log']);assert(r.output.some(row=>row.event==='native_test_complete'&&row.mode==='activity'));
});
test('native activity missing capability fails before log reads without leaking input',async()=>{
 const r=await run('activity',{capability:false});assert.equal(r.exit,1);sanitized(r);assert.deepEqual(r.f.requests.map(request=>request.op),['status']);assert(!r.output.some(row=>row.event==='activity_log'));
});
test('native activity invalid result fails generically without echoing unknown private fields',async()=>{
 const r=await run('activity',{page:page=>({...page,filename:'PRIVATE_NAME.pdf'})});assert.equal(r.exit,1);sanitized(r);assert(!JSON.stringify(r.output).includes('PRIVATE_NAME'));assert(r.output.some(row=>row.event==='native_test_failed'));
});
test('native portal mode binds the saved profile/revision and fully reads its pending ACK before disconnect',async()=>{
 const r=await run('portal',{pendingOp:'portal_handoff'});assert.equal(r.exit,0);sanitized(r);
 assert.deepEqual(r.f.requests.map(request=>request.op),['status','list','status','portal_begin','portal_handoff','result']);
 const prepared=r.f.requests.find(request=>request.op==='portal_begin'),handed=r.f.requests.find(request=>request.op==='portal_handoff');
 assert.equal(prepared.profile,1);assert.equal(prepared.revision,'6');assert.equal(handed.profile,1);assert.equal(handed.revision,'6');assert.equal(handed.grant,'42');
 assert(r.f.events.indexOf('result:read-complete')<r.f.events.indexOf('gatt:disconnect'));assert(r.f.events.indexOf('result:read-complete')<r.f.events.indexOf('bridge:disconnect'));
 assert.deepEqual(r.output.find(row=>row.event==='portal_handoff'),{event:'portal_handoff',ack_read:true,disconnect_requested:true});
});
test('native portal missing capability cannot prepare or scan',async()=>{
 const r=await run('portal',{metadata:meta=>{delete meta.csp.public_wifi;delete meta.csp.public_wifi_protocol;return meta;}});assert.equal(r.exit,1);sanitized(r);assert.deepEqual(r.f.requests.map(request=>request.op),['status']);
});
for(const status of [{durable:false},{wifi:'idle'},{wifi_profile:2},{selected_profile:0},{applied_revision:'5'}])test('native portal refuses an ineligible saved Wi-Fi state '+JSON.stringify(status),async()=>{
 const r=await run('portal',{wifiStatus:status});assert.equal(r.exit,1);sanitized(r);assert(!r.f.requests.some(request=>request.op==='portal_begin'));assert(!r.f.requests.some(request=>['scan','begin','stage','check','commit'].includes(request.op)));
});
test('native portal invalid handoff does not emit handoff success or expose instructions',async()=>{
 const r=await run('portal',{reply:op=>op==='portal_handoff'?{ok:false,error:3}:undefined});assert.equal(r.exit,1);sanitized(r);assert(!r.output.some(row=>row.event==='portal_handoff'));assert(r.output.some(row=>row.event==='native_test_failed'));
});
test('existing native identity mode keeps its behavior',async()=>{
 const r=await run('identity');assert.equal(r.exit,0);assert.deepEqual(r.f.requests.map(request=>request.op),['status']);assert.equal(r.output.find(row=>row.event==='authenticated').firmware,'SYNTHETIC');
});
test('existing native scan mode still returns only its network count',async()=>{
 const r=await run('scan');assert.equal(r.exit,0);assert.deepEqual(r.f.requests.map(request=>request.op),['status','scan','scan_page']);assert.equal(r.output.find(row=>row.event==='scan_complete').networks,1);assert(!JSON.stringify(r.output).includes('PRIVATE_SYNTHETIC_VENUE'));
});
