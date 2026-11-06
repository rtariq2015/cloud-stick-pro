// Actual app + ManagementConnection + PublicWifiClient. Only DOM/GATT and the
// cryptographic fixture are synthetic. Physical Bluetooth is never requested.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {WifiSetupClient,PublicWifiClient,WifiInputError} from '../../phone/wifi-setup/connection.mjs';
import {fixture as transport,VENUE,prepared,until} from './fixture.mjs';
const html=readFileSync(new URL('../../phone/wifi-setup/index.html',import.meta.url),'utf8');
const source=readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,'');
const KEY='PUBLIC_SYNTHETIC_OWNER_KEY_ONLY_32';
function fixture(options={}){
 class Element{
  hidden=false;disabled=false;checked=false;textContent='';value='';type='';children=[];
  append(child){this.children.push(child);}replaceChildren(...children){this.children=children;}
  set innerHTML(_){throw new Error('Unsafe markup rendering');}
 }
 const e={};for(const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)){
  const element=new Element();element.hidden=/\bhidden\b/.test(match[0]);element.disabled=/\bdisabled\b/.test(match[0]);e[match[1]]=element;
 }
 const windowEvents=new Map(),documentEvents=new Map(),connections=[],timers=[];
 const document={visibilityState:'visible',getElementById:id=>e[id],createElement:()=>new Element(),createTextNode:text=>({textContent:text}),addEventListener:(name,fn)=>documentEvents.set(name,fn)};
 const f=transport(options);let now=1793793600000;
 class TestDate extends Date{static now(){return now;}}
 const scope={ManagementConnection:class{constructor(next){const connection=f.create(next);connections.push(connection);return connection;}},
  WifiSetupClient,PublicWifiClient,WifiInputError,document,location:{protocol:'https:'},isSecureContext:true,navigator:{bluetooth:{requestDevice(){throw new Error('Unmocked Bluetooth request');}}},TextEncoder,Date:TestDate,
  setTimeout:fn=>{timers.push(fn);return timers.length;},addEventListener:(name,fn)=>windowEvents.set(name,fn)};
 vm.runInNewContext(source+'\nglobalThis.inspect=()=>({connected:!!connection,flow:!!flow,portal:!!portal,portalNetwork,portalPrepared,portalInstructions,selected,profiles});',scope);
 return {...f,e,document,get waiting(){return f.waiting;},inspect:()=>scope.inspect(),event:(name,event={persisted:true})=>(windowEvents.get(name)||documentEvents.get(name))?.(event),
  advance:ms=>now+=ms,runTimers:()=>timers.forEach(fn=>fn()),cleanup:()=>connections.forEach(connection=>connection.disconnect()),
  domText:()=>Object.values(e).map(element=>element.textContent+' '+element.value).join('\n')};
}
async function connect(f){f.e['owner-key'].value=KEY;await f.e.connect.onclick();assert.equal(f.e['connection-state'].textContent,'Private owner authenticated');}
async function prepare(f){await f.e['portal-prepare'].onclick();assert(!f.e['portal-ready'].hidden);}
function authScrubbed(f){
 const s=f.inspect();assert(!s.flow);assert(!s.portal);assert.equal(s.portalNetwork,null);assert.equal(s.portalPrepared,null);assert.equal(s.selected,null);assert.equal(s.profiles.length,0);
 assert.equal(f.e['owner-key'].value,'');assert.equal(f.e['wifi-password'].value,'');assert.equal(f.e['manual-ssid'].value,'');
 assert.equal(f.e.identity.textContent,'');assert.equal(f.e['portal-network'].textContent,'');assert.equal(f.e['saved-message'].textContent,'');
 assert(f.e['wifi-card'].hidden);assert(f.e['portal-card'].hidden);assert(f.e['portal-ready'].hidden);
 assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');assert(!f.domText().includes(KEY));
}
test('public helper is offered only after authenticated saved-profile status',async t=>{
 const f=fixture();t.after(f.cleanup);assert(f.e['portal-card'].hidden);await connect(f);
 assert(!f.e['portal-card'].hidden);assert.match(f.e['portal-network'].textContent,/Saved network/);
 assert.deepEqual(f.requests.map(r=>r.op),['list','status']);assert.equal(f.e['owner-key'].value,'');
});
for(const [name,status] of [
 ['not durable',{durable:false}],['not connected',{wifi:'idle'}],['wrong active profile',{wifi_profile:2}],
 ['no saved selection',{selected_profile:0}],['stale applied revision',{applied_revision:'5'}],
])test('helper is hidden for '+name,async t=>{
 const f=fixture({status});t.after(f.cleanup);await connect(f);assert(f.e['portal-card'].hidden);assert(f.e['portal-prepare'].disabled);
});
test('default capability hides helper and sends no portal request',async t=>{
 const f=fixture({metadata:meta=>{delete meta.csp.public_wifi;delete meta.csp.public_wifi_protocol;return meta;}});t.after(f.cleanup);await connect(f);
 assert(f.e['portal-card'].hidden);await f.e['portal-prepare'].onclick();assert(!f.requests.some(r=>r.op.startsWith('portal_')));
});
test('prepare freezes profile-changing controls until cancel consumes the grant',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);await prepare(f);
 for(const id of ['scan','manual','check','save','manual-ssid','manual-open','wifi-password','cancel'])assert(f.e[id].disabled,id);
 const before=f.requests.length;await f.e.scan.onclick();await f.e.manual.onclick();await f.e.check.onclick();assert.equal(f.requests.length,before);
 await f.e['portal-cancel'].onclick();assert(f.e['portal-ready'].hidden);assert.equal(f.inspect().portalPrepared,null);assert(!f.e.scan.disabled);
 assert.deepEqual(f.requests.at(-1),{v:2,id:f.requests.at(-1).id,op:'portal_cancel',grant:'42'});
});
test('Continue fetches pending result before deliberate disconnect and leaves instruction-only state',async t=>{
 const f=fixture({pendingOp:'portal_handoff'});t.after(f.cleanup);await connect(f);await prepare(f);await f.e['portal-continue'].onclick();
 assert(f.events.indexOf('result:read-complete')<f.events.lastIndexOf('gatt:disconnect'));authScrubbed(f);
 assert(!f.e['portal-handoff-card'].hidden);assert.equal(f.e['portal-ap-name'].textContent,prepared.ap_ssid);assert.equal(f.e['portal-ap-password'].textContent,'admin123');
 assert.match(f.e['portal-handoff-state'].textContent,/Bluetooth is closed/);
 assert(!f.domText().includes(VENUE));assert(!JSON.stringify(f.inspect().portalInstructions).includes('grant'));
 assert(!JSON.stringify(f.inspect().portalInstructions).includes('revision'));assert(!/cloud.*verified|upload.*succeed/i.test(f.e['portal-handoff-state'].textContent));
});
for(const reason of ['disconnect','pagehide','pageshow','freeze','resume','visibilitychange'])test(reason+' clears private preparation and keeps no AP claim before handoff',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);await prepare(f);
 if(reason==='disconnect')f.e.disconnect.onclick();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}
 authScrubbed(f);assert(f.e['portal-handoff-card'].hidden);assert.equal(f.inspect().portalInstructions,null);
});
for(const phase of ['portal_begin','portal_handoff'])for(const reason of ['disconnect','pagehide','freeze','visibilitychange'])test('late '+phase+' after '+reason+' cannot paint prepared/authenticated/instruction state',async t=>{
 const f=fixture({delayOp:phase});t.after(f.cleanup);await connect(f);if(phase==='portal_handoff')await prepare(f);
 const pending=phase==='portal_begin'?f.e['portal-prepare'].onclick():f.e['portal-continue'].onclick();await until(()=>f.waiting);
 if(reason==='disconnect')f.e.disconnect.onclick();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}
 f.release();await pending;authScrubbed(f);assert(f.e['portal-handoff-card'].hidden);
});
for(const reason of ['pagehide','pageshow','freeze','resume','visibilitychange'])test('public AP instructions survive Settings '+reason+' while authentication stays cleared',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);await prepare(f);await f.e['portal-continue'].onclick();
 if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);authScrubbed(f);
 assert(!f.e['portal-handoff-card'].hidden);assert.equal(f.e['portal-ap-name'].textContent,prepared.ap_ssid);assert(!f.domText().includes(VENUE));
});
test('expired preparation cancels instead of transmitting handoff',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);await prepare(f);f.advance(30001);await f.e['portal-continue'].onclick();
 assert.equal(f.requests.at(-1).op,'portal_cancel');assert(!f.requests.some(r=>r.op==='portal_handoff'));assert(f.e['portal-ready'].hidden);assert.match(f.e['portal-message'].textContent,/expired/);
});
test('expired public instructions claim no ongoing authentication, cloud probe or upload',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);await prepare(f);await f.e['portal-continue'].onclick();f.advance(300001);f.runTimers();
 assert.match(f.e['portal-handoff-state'].textContent,/window has ended/);assert.match(f.e['portal-handoff-state'].textContent,/cannot confirm the result/);authScrubbed(f);
});
test('new authenticated reconnect hides old instruction-only card',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);await prepare(f);await f.e['portal-continue'].onclick();assert(!f.e['portal-handoff-card'].hidden);
 await connect(f);assert(f.e['portal-handoff-card'].hidden);assert.equal(f.inspect().portalInstructions,null);
});
test('disconnect scrubs selected venue SSID from hidden DOM as well as variables',async t=>{
 const f=fixture();t.after(f.cleanup);await connect(f);f.e['manual-ssid'].value=VENUE;f.e['manual-open'].checked=true;await f.e.manual.onclick();assert.equal(f.e['selected-name'].textContent,VENUE);
 f.e.disconnect.onclick();authScrubbed(f);assert.equal(f.e['selected-name'].textContent,'');assert(!f.domText().includes(VENUE));
});
test('negative changed-revision handoff removes unusable Continue state instead of retrying a spent grant',async t=>{
 const f=fixture({reply:op=>op==='portal_handoff'?{ok:false,error:3}:undefined});t.after(f.cleanup);await connect(f);await prepare(f);await f.e['portal-continue'].onclick();
 assert(f.e['portal-ready'].hidden,'Continue must not offer an already consumed grant');assert.equal(f.inspect().portalPrepared,null);
 assert(f.e['portal-handoff-card'].hidden);assert.equal(f.requests.filter(r=>r.op==='portal_handoff').length,1);
});
test('malformed cancel acknowledgment clears private session and cannot show AP instructions',async t=>{
 const f=fixture({reply:op=>op==='portal_cancel'?{ok:true,phase:'active',portal_error:0,remaining_ms:9000,attempt_dav_read_verified:true}:undefined});t.after(f.cleanup);
 await connect(f);await prepare(f);await f.e['portal-cancel'].onclick();authScrubbed(f);assert(f.e['portal-handoff-card'].hidden);
 assert(!f.domText().includes('PRIVATE_DO_NOT_REFLECT'));
});
