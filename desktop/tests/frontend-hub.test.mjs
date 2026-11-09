// SPDX-License-Identifier: MIT
import test from 'node:test';import assert from 'node:assert/strict';
import {inventory,watchUsb} from '../frontend/hub.mjs';
test('metadata discovery does not select among multiple sticks or accept malformed inventory',()=>{
 assert.deepEqual(inventory([{port:'COM4',label:'B'},{port:'COM3',label:'A'}]).map(x=>x.port),['COM3','COM4']);
 for(const rows of [null,[{port:'COM3',label:'bad\nlabel'}],[{port:'COM3',label:'A'},{port:'COM3',label:'B'}]])assert.throws(()=>inventory(rows));
});
test('hotplug watches metadata only while idle, debounces unchanged rows, and stops cleanly',async()=>{
 let idle=true,timer,changed=0,calls=0,rows=[];
 const watcher=watchUsb({idle:()=>idle,request:async op=>{assert.equal(op,'usb_watch_ports');calls++;return rows;},changed:async()=>{changed++;},setTimer:f=>(timer=f,1),clearTimer:()=>{timer=null;}});
 await watcher.start();assert.equal(changed,1);await timer();assert.equal(changed,1);
 rows=[{port:'COM3',label:'Stick'}];idle=false;await timer();assert.equal(calls,2);
 idle=true;await timer();assert.equal(changed,2);watcher.stop();assert.equal(timer,null);
});
test('a discovery result returned after shutdown cannot touch the wizard',async()=>{
 let resolve,changed=0;const watcher=watchUsb({idle:()=>true,request:()=>new Promise(r=>{resolve=r;}),changed:()=>changed++,setTimer:()=>1,clearTimer:()=>{}});
 const pending=watcher.start();watcher.stop();resolve([{port:'COM3',label:'Stick'}]);await pending;assert.equal(changed,0);
});

test('replacing a device on the same port invalidates its previously checked identity',()=>{const a=inventory([{port:'COM3',label:'Stick',token:'a'.repeat(16)}]),b=inventory([{port:'COM3',label:'Stick',token:'b'.repeat(16)}]);assert.notDeepEqual(a,b);});

import {mountNavigation} from '../frontend/hub.mjs';
function navigationFixture(){
 const make=(dataset={})=>({dataset,tagName:'BUTTON',disabled:false,attrs:{},listeners:{},addEventListener(k,f){this.listeners[k]=f;},setAttribute(k,v){this.attrs[k]=v;},removeAttribute(k){delete this.attrs[k];}});
 const buttons=['overview','manage','setup','updates'].map(appPage=>make({appPage})),tabs=['overview','wifi','cloud','history'].map(managerView=>make({managerView}));
 const nodes={ 'workspace-title':{},'navigation-message':{} },document={body:{dataset:{}},getElementById:id=>nodes[id],querySelectorAll:s=>s==='[data-app-page]'?buttons:tabs};
 let current={busy:false,authenticated:false,prepared:false,step:'welcome'},manage=0,setup=0;
 const nav=mountNavigation({document,state:()=>current,openManage(){manage++;},openSetup(){setup++;}});
 return {nav,document,buttons,tabs,nodes,current,get calls(){return {manage,setup};}};
}
test('workspace navigation opens one task and keeps a device operation on its current screen',()=>{
 const f=navigationFixture();assert.equal(f.document.body.dataset.page,'overview');assert.equal(f.nav.go('updates'),true);assert.deepEqual(f.calls,{manage:0,setup:0});
 f.current.busy=true;f.nav.refresh();assert.equal(f.nav.go('manage'),false);assert.equal(f.buttons[1].disabled,true);assert.equal(f.buttons[3].disabled,false);
 f.current.busy=false;assert.equal(f.nav.go('manage'),true);assert.equal(f.calls.manage,1);assert.equal(f.nodes['workspace-title'].textContent,'My stick');
 f.current.authenticated=true;f.nav.refresh();assert.equal(f.nav.go('overview'),false);assert.match(f.nodes['navigation-message'].textContent,/Bluetooth is open/);
 f.current.authenticated=false;assert.equal(f.nav.go('setup'),true);assert.equal(f.calls.setup,1);assert.equal(f.nav.go('untrusted'),false);
});
test('authenticated management promotes its screen and its section tabs never start device operations',()=>{
 const f=navigationFixture();f.current.step='records';f.nav.refresh();assert.equal(f.document.body.dataset.page,'manage');
 f.tabs[1].listeners.click();assert.equal(f.document.body.dataset.managerView,'wifi');assert.equal(f.tabs[1].attrs['aria-selected'],'true');assert.equal(f.tabs[0].attrs['aria-selected'],'false');assert.deepEqual(f.calls,{manage:0,setup:0});
});
