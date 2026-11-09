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
