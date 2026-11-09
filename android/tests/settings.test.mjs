// SPDX-License-Identifier: MIT
import test from 'node:test';import assert from 'node:assert/strict';import {mountSettings} from '../app/src/main/assets/settings.mjs';
function fixture({save=async()=>{},check=async()=>{}}={}){
 const elements=new Map(),calls=[],locks=[],messages=[];let active=true;
 const $=id=>{if(!elements.has(id))elements.set(id,{value:'',hidden:false,disabled:false,checked:false,children:[],listeners:{},addEventListener(k,f){this.listeners[k]=f;},append(x){this.children.push(x);},replaceChildren(){this.children=[];}});return elements.get(id);};
 const document={getElementById:$,createElement:()=>({})};const conn={disconnect(){active=false;calls.push('disconnect');}};
 const wifi={profiles:async()=>[{id:7,ssid:'Synthetic Wi-Fi',open:false,has_secret:true,enabled:true,priority:3}],scan:async()=>[],stage:async row=>{calls.push(['stage',row]);},check:async()=>{calls.push('check');await check();},save:async()=>{calls.push('save');await save();},abort:async()=>calls.push('abort')};
 const panel=mountSettings({document,connection:()=>active?conn:null,lock:v=>locks.push(v),message:v=>messages.push(v),wifiFactory:()=>wifi});panel.show();
 return {$,panel,calls,locks,messages,click:id=>$(id).listeners.click()};
}
test('Wi-Fi check keeps only the same saved network secret and needs an explicit Save',async()=>{
 const f=fixture();f.$('settings-ssid').value='Synthetic Wi-Fi';await f.click('settings-check');
 assert.deepEqual(f.calls[0],['stage',{ssid:'Synthetic Wi-Fi',password:'',open:false,profile:7,keep:true,priority:3,enabled:true}]);assert.equal(f.calls.includes('save'),false);
 assert.equal(f.$('settings-wifi-save').hidden,false);assert.equal(f.$('settings-ssid').disabled,true);assert.equal(f.locks.at(-1),true);
 await f.click('settings-wifi-save');assert.equal(f.calls.at(-1),'save');assert.equal(f.$('settings-wifi-save').hidden,true);assert.equal(f.locks.at(-1),false);
});
test('an ambiguous Save disconnects and never retries or claims success',async()=>{
 const f=fixture({save:async()=>{throw Error('synthetic');}});f.$('settings-ssid').value='New network';f.$('settings-password').value='synthetic-password';await f.click('settings-check');await f.click('settings-wifi-save');
 assert.equal(f.calls.filter(c=>c==='save').length,1);assert.ok(f.calls.includes('disconnect'));assert.equal(f.$('stick-settings').hidden,true);assert.equal(f.$('settings-password').value,'');assert.match(f.messages.at(-1),/not confirmed/);
});
test('closing clears credentials and a late connection test cannot reopen Save',async()=>{
 let finish;const f=fixture({check:()=>new Promise(r=>{finish=r;})});f.$('settings-ssid').value='Synthetic';f.$('settings-password').value='synthetic-password';const job=f.click('settings-check');while(!finish)await Promise.resolve();f.panel.clear();finish();await job;
 assert.equal(f.$('stick-settings').hidden,true);assert.equal(f.$('settings-wifi-save').hidden,true);assert.equal(f.$('settings-password').value,'');
});
