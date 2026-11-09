// SPDX-License-Identifier: MIT
import test from 'node:test';import assert from 'node:assert/strict';import {mountSettings} from '../app/src/main/assets/settings.mjs';
function fixture({save=async()=>{},check=async()=>{},rows=[{id:7,ssid:'Synthetic Wi-Fi',open:false,has_secret:true,enabled:true,priority:3}]}={}){
 const elements=new Map(),calls=[],locks=[],messages=[];let active=true;
 const $=id=>{if(!elements.has(id))elements.set(id,{value:'',hidden:false,disabled:false,checked:false,children:[],listeners:{},addEventListener(k,f){this.listeners[k]=f;},append(x){this.children.push(x);},replaceChildren(){this.children=[];}});return elements.get(id);};
 const make=()=>({value:'',hidden:false,disabled:false,checked:false,children:[],listeners:{},addEventListener(k,f){this.listeners[k]=f;},append(...items){this.children.push(...items);},replaceChildren(){this.children=[];},setAttribute(){}});
 const document={getElementById:$,createElement:make};const conn={disconnect(){active=false;calls.push('disconnect');}};
 const wifi={profiles:async()=>rows,removeProfiles:async value=>{calls.push(['remove',value]);},scan:async()=>[],stage:async row=>{calls.push(['stage',row]);},check:async()=>{calls.push('check');await check();},save:async()=>{calls.push('save');await save();},abort:async()=>calls.push('abort')};
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
test('network removal reviews unchecked rows, checks a retained network and waits for Save',async()=>{
 const rows=[{id:7,ssid:'Synthetic home',enabled:true,priority:0},{id:8,ssid:'Synthetic hotspot',enabled:true,priority:1}],f=fixture({rows});await f.click('settings-load');
 const box=f.$('settings-saved').children[1].children[0].children[0];box.checked=false;box.listeners.change();
 f.$('settings-keep-network').value='7';assert.equal(f.$('settings-removal').hidden,false);await f.click('settings-remove-check');
 assert.deepEqual(f.calls.slice(0,2),[['remove',{profiles:[8],keep:7}],'check']);assert.equal(f.calls.includes('save'),false);assert.equal(f.$('settings-wifi-save').textContent,'Save network removals');assert.equal(f.locks.at(-1),true);
 await f.click('settings-wifi-save');assert.equal(f.calls.filter(x=>x==='save').length,1);assert.equal(f.locks.at(-1),false);
});
test('last enabled network cannot be unchecked, and cancelling reviewed removal never saves',async()=>{
 const f=fixture();await f.click('settings-load');const last=f.$('settings-saved').children[0].children[0].children[0];last.checked=false;last.listeners.change();assert.equal(last.checked,true);assert.match(f.messages.at(-1),/at least one enabled/);assert.equal(f.calls.length,0);
 const g=fixture({rows:[{id:7,ssid:'Home',enabled:true,priority:0},{id:8,ssid:'Hotspot',enabled:true,priority:1}]});await g.click('settings-load');const box=g.$('settings-saved').children[1].children[0].children[0];box.checked=false;box.listeners.change();g.$('settings-keep-network').value='7';await g.click('settings-remove-check');await g.click('settings-cancel');
 assert.equal(g.calls.includes('save'),false);assert.equal(g.calls.at(-1),'abort');assert.equal(g.$('settings-wifi-save').hidden,true);assert.equal(g.$('settings-removal').hidden,true);
});
