// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {mountAppUpdates as desktop} from '../frontend/hub.mjs';
import {mountAppUpdates as android} from '../../android/app/src/main/assets/updates.mjs';

const state={phase:'current',current:'1.2.3',version:'',done:0,total:0};
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(mount,{busy=false,hidden=false,request=async()=>state}={}){
 const nodes=new Map(),listeners=new Map(),timers=new Map();let sequence=0;
 const document={hidden,addEventListener:(name,fn)=>listeners.set(name,fn),
  removeEventListener:(name,fn)=>{if(listeners.get(name)===fn)listeners.delete(name);},
  getElementById(id){if(!nodes.has(id))nodes.set(id,{textContent:'',addEventListener(name,fn){this[name]=fn;}});return nodes.get(id);}};
 const calls=[],controller=mount({document,idle:()=>!busy,request:async op=>{calls.push(op);return request(op);},
  setTimer(fn,ms){const id=++sequence;timers.set(id,{fn,ms});return id;},clearTimer:id=>timers.delete(id)});
 return {controller,document,nodes,listeners,timers,calls,setBusy(value){busy=value;},
  async tick(){assert.equal(timers.size,1);const [id,{fn}]=timers.entries().next().value;timers.delete(id);fn();await flush();},
  async visible(value){document.hidden=!value;listeners.get('visibilitychange')?.();await flush();}};
}
for(const [name,mount]of [['desktop',desktop],['Android',android]]){
 test(name+': visibility cannot start an unstarted controller',async()=>{
  const f=fixture(mount);await f.visible(true);assert.equal(f.calls.length,0);f.controller.start();await flush();assert.equal(f.calls.length,1);f.controller.stop();
 });
 test(name+': first app update waits for USB/firmware work, then runs once',async()=>{
  const f=fixture(mount,{busy:true});f.controller.start();f.controller.start();assert.deepEqual(f.calls,[]);assert.equal(f.timers.size,1);
  assert.equal([...f.timers.values()][0].ms,2000);await f.tick();assert.deepEqual(f.calls,[]);
  f.setBusy(false);await f.tick();assert.deepEqual(f.calls,['app_update_check']);assert.equal(f.nodes.get('app-update-current').textContent,'App 1.2.3');assert.equal(f.timers.size,0);
  f.controller.start();assert.equal(f.calls.length,1);f.controller.stop();
 });
 test(name+': hidden startup resumes on visibility without background retries',async()=>{
  const f=fixture(mount,{hidden:true});f.controller.start();assert.equal(f.calls.length,0);assert.equal(f.timers.size,0);
  await f.visible(true);assert.deepEqual(f.calls,['app_update_check']);await f.visible(false);await f.visible(true);assert.equal(f.calls.length,1);f.controller.stop();
 });
 test(name+': stop cancels a deferred initial check and detaches visibility',async()=>{
  const f=fixture(mount,{busy:true});f.controller.start();const late=[...f.timers.values()][0].fn;f.controller.stop();f.setBusy(false);
  assert.equal(f.timers.size,0);assert.equal(f.listeners.size,0);late();f.controller.start();await flush();assert.equal(f.calls.length,0);
 });
 test(name+': actual check failure is shown and does not retry automatically',async()=>{
  const f=fixture(mount,{request:async()=>{throw Error('offline');}});f.controller.start();await flush();
  assert.deepEqual(f.calls,['app_update_check']);assert.equal(f.timers.size,0);assert.match(f.nodes.get('app-update-message').textContent,/try again/i);
  await f.visible(false);await f.visible(true);assert.equal(f.calls.length,1);f.controller.stop();
 });
 test(name+': manual check consumes the deferred startup attempt',async()=>{
  const f=fixture(mount,{busy:true});f.controller.start();const late=[...f.timers.values()][0].fn;f.setBusy(false);f.nodes.get('app-update-check').click();await flush();late();await flush();
  assert.deepEqual(f.calls,['app_update_check']);assert.equal(f.timers.size,0);f.controller.stop();
 });
 test(name+': stopping during a request cannot repaint or reschedule',async()=>{
  let resolve;const f=fixture(mount,{request:()=>new Promise(r=>{resolve=r;})});f.controller.start();f.controller.stop();resolve({...state,phase:'checking'});await flush();
  assert.equal(f.nodes.has('app-update-current'),false);assert.equal(f.timers.size,0);
 });
 test(name+': a started check continues its bounded state polling',async()=>{
  let first=true;const f=fixture(mount,{request:async()=>{if(first){first=false;return {...state,phase:'checking'};}return state;}});f.controller.start();await flush();
  assert.deepEqual(f.calls,['app_update_check']);assert.equal([...f.timers.values()][0].ms,2500);await f.tick();assert.deepEqual(f.calls,['app_update_check','app_update_state']);assert.equal(f.timers.size,0);f.controller.stop();
 });
}
