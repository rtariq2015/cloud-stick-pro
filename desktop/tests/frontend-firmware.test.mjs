import test from 'node:test';import assert from 'node:assert/strict';
import {firmwareState,firmwareMessage,mountFirmwarePanel} from '../frontend/firmware.mjs';
import {DesktopBridge} from '../frontend/bridge.mjs';
const state=(phase='available',extra={})=>({phase,done:1,total:1,code:'',online:false,version:'0.4.6-cache-dev',...extra});
test('native state is exact and cannot supply HTML, paths or credential fields',()=>{
 assert.equal(firmwareState(state()).phase,'available');
 for(const change of [{phase:'<script>'},{done:true},{total:0},{done:2},{code:'private path /secret'},{password:'private'},{can_resume:'yes'}])assert.throws(()=>firmwareState(state('available',change)));
 assert.match(firmwareMessage(state()),/Online check unavailable; using the verified copy/);
 assert.match(firmwareMessage(state('installed')),/restart checked/);
 assert.doesNotMatch(firmwareMessage(state('verified')),/installed and the restart checked/);
});
function ui(initial=state()){
 class Element{constructor(){this.value='';this.disabled=false;this.hidden=false;this.listeners={};this.children=[];}addEventListener(k,v){this.listeners[k]=v;}append(...x){this.children.push(...x);}replaceChildren(){this.children=[];}focus(){}click(){if(!this.disabled)return this.listeners.click?.();}}
 const elements=new Map(),get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};get('firmware-mode').value='existing';
 let next=initial,busy=false,allowed=true,continued=0;const calls=[],timers=[];
 const panel=mountFirmwarePanel({document:{getElementById:get,createElement:()=>new Element()},request:async(op,args)=>{calls.push([op,args]);if(op==='firmware_ports')return [{port:'COM3',label:'COM3 · ESP32-S3 USB'}];return next;},canUse:()=>allowed,setBusy:x=>busy=x,onContinue:()=>continued++,setTimer:f=>{timers.push(f);return 1;},clearTimer:()=>{}});
 return {panel,get,calls,timers,setState:x=>next=x,setAllowed:x=>allowed=x,get busy(){return busy;},get continued(){return continued;}};
}
test('existing update requires preparation and offers only fixed native operations',async()=>{
 const f=ui();await f.get('firmware-check').click();await f.get('firmware-ports-refresh').click();assert.equal(f.get('firmware-install').disabled,true);assert.equal(f.get('firmware-prepare').disabled,false);
 f.setState(state('preparing',{done:0}));await f.get('firmware-prepare').click();assert.equal(f.busy,true);assert.equal(f.get('firmware-install').disabled,true);
 f.setState(state('prepared'));await f.panel.restore();assert.equal(f.get('firmware-install').disabled,false);
 f.setState(state('starting',{done:0}));await f.get('firmware-install').click();assert.deepEqual(f.calls.at(-1),['firmware_start',{port:'COM3',mode:'existing'}]);assert.equal(f.busy,true);assert.equal(f.get('firmware-continue').hidden,true);
});
test('reopened interrupted job stays locked until an explicit resume',async()=>{
 const f=ui(state('recovery_required',{can_resume:true}));await f.panel.restore();assert.equal(f.busy,true);assert.equal(f.get('firmware-check').disabled,true);assert.equal(f.get('firmware-resume').hidden,false);assert.equal(f.calls.filter(([op])=>op==='firmware_start').length,0);
 await f.get('firmware-ports-refresh').click();await f.get('firmware-resume').click();assert.deepEqual(f.calls.at(-1),['firmware_start',{port:'COM3',mode:'resume'}]);
});
test('completion returns to setup and a native rejection shows fixed help',async()=>{
 const f=ui(state('installed'));await f.panel.restore();assert.equal(f.busy,false);assert.equal(f.get('firmware-continue').hidden,false);f.get('firmware-continue').click();assert.equal(f.continued,1);
 f.setAllowed(false);f.panel.refreshLocks();assert.equal(f.get('firmware-check').disabled,true);
});
test('renderer can never supply a URL, image, native approval, filename or flash offset',async()=>{
 const signal=()=>({connect(){},disconnect(){}}),calls=[];
 const bridge=new DesktopBridge({request:x=>calls.push(x),response:signal(),events:signal()},{setTimer:()=>1,clearTimer:()=>{}});
 for(const field of ['url','image','approved','approval','path','offset','signature'])await assert.rejects(bridge.request('firmware_start',{port:'COM3',mode:'new',[field]:'forged'}));
 assert.equal(calls.length,0);bridge.close();
});
