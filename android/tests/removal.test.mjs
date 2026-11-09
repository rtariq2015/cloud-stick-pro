// SPDX-License-Identifier: MIT
import test from 'node:test';import assert from 'node:assert/strict';import {WifiSetupClient} from '../app/src/main/assets/shared/connection.mjs';
function fixture(){
 let saved=[{id:1,ssid:'Synthetic home',open:false,has_secret:true,enabled:true,priority:0},{id:2,ssid:'Synthetic backup',open:false,has_secret:true,enabled:true,priority:1}],candidate=null,edit=0,phase=1,keep=1;const calls=[];
 const status=()=>({ok:true,revision:'7',durable:true,store_fault:false,internet_verified:false,cloud_available:false,backups_paused:true,wifi:'local_connected',wifi_error:0,wifi_profile:keep,selected_profile:1,applied_revision:'7',...(candidate?{transaction:'11',edit,phase,check_error:phase===3?20:0}:{})});
 const connection={disconnect(){calls.push(['disconnect']);},async command(op,args={}){
  calls.push([op,args]);
  if(op==='list')return {ok:true,revision:'7',count:saved.length,next:0,profiles:saved};
  if(op==='begin'){candidate=saved.map(x=>({...x}));edit=0;return {ok:true,transaction:'11',edit:0};}
  if(op==='forget'){if(!candidate.some(x=>x.id===args.profile))return {ok:false,error:3};if(candidate.filter(x=>x.enabled).length===1&&candidate.find(x=>x.id===args.profile).enabled)return {ok:false,error:7};candidate=candidate.filter(x=>x.id!==args.profile);edit++;return {ok:true};}
  if(op==='status')return status();
  if(op==='check'){keep=args.profile;phase=3;return {ok:true};}
  if(op==='commit'){assert.equal(phase,3);assert.equal(args.edit,edit);assert.equal(args.accept_local,true);saved=candidate;candidate=null;return {ok:true,durable:true,revision:'8',internet_verified:false};}
  if(op==='abort'){candidate=null;return {ok:true};}
  throw Error('unexpected');
 }};
 return {client:new WifiSetupClient(connection),connection,calls,saved:()=>saved};
}
test('removal uses existing owner commands, checks the retained network and commits only on Save',async()=>{
 const f=fixture();await f.client.profiles();await f.client.removeProfiles({profiles:[1],keep:2});assert.equal(f.saved().length,2);assert.equal(f.calls.some(([op])=>op==='commit'),false);
 await f.client.check();assert.equal(f.calls.some(([op])=>op==='commit'),false);await f.client.save();assert.deepEqual(f.saved().map(x=>x.id),[2]);
 assert.deepEqual(f.calls.find(([op])=>op==='forget')[1],{transaction:'11',profile:1,confirm:false});assert.equal(f.calls.some(([,args])=>args&&Object.hasOwn(args,'password')),false);
});
test('cancelling a removal leaves the durable network list intact',async()=>{const f=fixture();await f.client.profiles();await f.client.removeProfiles({profiles:[1],keep:2});await f.client.abort();assert.deepEqual(f.saved().map(x=>x.id),[1,2]);assert.equal(f.calls.some(([op])=>op==='commit'),false);});
test('empty, duplicate, overlapping and oversized removal selections never start a transaction',async()=>{const f=fixture();await f.client.profiles();for(const value of [{profiles:[],keep:1},{profiles:[1,1],keep:2},{profiles:[1],keep:1},{profiles:[0],keep:2},{profiles:[1],keep:0},{profiles:[1,2,3,4,5,6,7,8],keep:9}])await assert.rejects(f.client.removeProfiles(value));assert.equal(f.calls.some(([op])=>op==='begin'),false);});
test('a stale removal acknowledgment ends the session before any commit',async()=>{
 const f=fixture(),original=f.connection.command;f.connection.command=async(op,args)=>{const answer=await original(op,args);return op==='status'?{...answer,edit:999}:answer;};await f.client.profiles();await assert.rejects(f.client.removeProfiles({profiles:[1],keep:2}));assert.ok(f.calls.some(([op])=>op==='disconnect'));assert.equal(f.calls.some(([op])=>op==='commit'),false);
});
