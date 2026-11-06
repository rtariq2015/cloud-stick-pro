// Actual app body + connection class, synthetic DOM/GATT/crypto only. These
// deterministic event tests do not establish browser BFCache or radio behavior.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {ManagementConnection,VERSION,SESSION,INFO} from '../connection.mjs';
const encoder=new TextEncoder(),decoder=new TextDecoder();
const info={v:1,ok:true,model:'SYNTHETIC',device_id:'CSP-M1-SYNTHETIC',firmware:'SYNTHETIC',wifi_connected:true,backups_paused:true,synthetic:true};
const view=b=>new DataView(b.buffer,b.byteOffset,b.byteLength);
const tick=()=>new Promise(resolve=>setTimeout(resolve,0));
function fixture({delay=''}={}){
 let request,versionCache=new Uint8Array(),destroyed=0,choosers=0,authentications=0,release;
 const gate=stage=>stage===delay?new Promise(resolve=>{release=resolve;}):Promise.resolve();
 const windowEvents=new Map(),documentEvents=new Map(),deviceEvents=new Map();
 const characteristics={
  [VERSION]:{async writeValueWithResponse(data){assert.equal(decoder.decode(data),'---');versionCache=encoder.encode(JSON.stringify({prov:{sec_ver:2,sec_patch_ver:1},csp:{milestone:1,synthetic:true}}));},async readValue(){return view(versionCache);}},
  [SESSION]:{async writeValueWithResponse(){},async readValue(){return view(Uint8Array.of(1));}},
  [INFO]:{async writeValueWithResponse(data){request=JSON.parse(decoder.decode(data));},async readValue(){return view(encoder.encode(JSON.stringify({...info,id:request.id})));}}
 };
 const gatt={connected:false,async connect(){await gate('gatt');this.connected=true;return {async getPrimaryService(){return {async getCharacteristic(id){return characteristics[id];}};}};},disconnect(){this.connected=false;versionCache=new Uint8Array();}};
 const device={gatt,addEventListener:(k,fn)=>deviceEvents.set(k,fn),removeEventListener:k=>deviceEvents.delete(k)};
 const bluetooth={async requestDevice(){choosers++;await gate('chooser');return device;}};
 const securityFactory=()=>({async start(){authentications++;await gate('crypto');return Uint8Array.of(1);},async challenge(){return Uint8Array.of(2);},finish(){},async encrypt(b){return b;},async decrypt(b){await gate('decrypt');return b;},destroy(){destroyed++;}});
 class InjectedConnection extends ManagementConnection {constructor(opts){super({...opts,bluetooth,secureContext:true,securityFactory,timeoutMs:1000});}}
 const ids=['state-pill','state-message','connect','session-actions','connection-title','device-card','device-title','device-id','firmware','wifi','backups','preview-banner','refresh','disconnect'];
 const elements=Object.fromEntries(ids.map(id=>[id,{hidden:['device-card','session-actions','preview-banner'].includes(id),disabled:false,textContent:'',className:''}]));
 const document={visibilityState:'visible',getElementById:id=>elements[id],addEventListener:(k,fn)=>documentEvents.set(k,fn)};
 const context={ManagementConnection:InjectedConnection,document,location:{search:''},URLSearchParams,navigator:{bluetooth},isSecureContext:true,addEventListener:(k,fn)=>windowEvents.set(k,fn)};
 vm.runInNewContext(readFileSync(new URL('../app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,''),context);
 const event=name=>(windowEvents.get(name)||documentEvents.get(name))?.({persisted:true});
 const cleared=()=>{assert.notEqual(elements['state-pill'].textContent,'Secure test session');assert(elements['device-card'].hidden);assert(elements['session-actions'].hidden);for(const id of ['device-title','device-id','firmware','wifi','backups'])assert.equal(elements[id].textContent,'');};
 return {elements,document,gatt,event,cleared,deviceEvents,release:()=>release(),get destroyed(){return destroyed;},get choosers(){return choosers;},get authentications(){return authentications;}};
}
for(const teardown of ['disconnect','remote','pagehide','hidden','freeze'])test(teardown+' clears authenticated identity; restoration never reconnects',async()=>{
 const f=fixture();await f.elements.connect.onclick();assert.equal(f.elements['state-pill'].textContent,'Secure test session');assert.equal(f.authentications,1);
 if(teardown==='disconnect')f.elements.disconnect.onclick();
 else if(teardown==='remote')f.deviceEvents.get('gattserverdisconnected')();
 else if(teardown==='hidden'){f.document.visibilityState='hidden';f.event('visibilitychange');}
 else f.event(teardown);
 f.cleared();assert(!f.gatt.connected);assert.equal(f.destroyed,1);
 f.document.visibilityState='visible';f.event('visibilitychange');f.event('pageshow');f.event('resume');f.cleared();assert.equal(f.choosers,1);assert.equal(f.authentications,1);
 await f.elements.refresh.onclick();f.cleared();assert.match(f.elements['state-message'].textContent,/Find Cloud Stick/);
 await f.elements.connect.onclick();assert.equal(f.authentications,2);assert.equal(f.choosers,2);assert.equal(f.elements['state-pill'].textContent,'Secure test session');f.elements.disconnect.onclick();
});
for(const stage of ['chooser','gatt','crypto','decrypt'])test('late '+stage+' completion after pagehide/restore cannot repaint secure state',async()=>{
 const f=fixture({delay:stage}),pending=f.elements.connect.onclick();await tick();f.event('pagehide');f.event('pageshow');f.cleared();await f.elements.connect.onclick();assert.match(f.elements['state-message'].textContent,/still finishing/);assert.equal(f.choosers,1);f.release();await pending;await tick();f.cleared();assert(!f.gatt.connected);assert.equal(f.choosers,1);f.elements.disconnect.onclick();
});
test('hidden/frozen pages refuse connection actions until visible and restored',async()=>{
 const f=fixture();f.document.visibilityState='hidden';f.event('visibilitychange');await f.elements.connect.onclick();assert.equal(f.choosers,0);f.cleared();
 f.document.visibilityState='visible';f.event('freeze');await f.elements.connect.onclick();assert.equal(f.choosers,0);f.cleared();f.event('resume');await f.elements.connect.onclick();assert.equal(f.choosers,1);f.elements.disconnect.onclick();
});
