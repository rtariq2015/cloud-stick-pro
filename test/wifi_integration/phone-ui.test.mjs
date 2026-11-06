// Actual app body + actual WifiSetupClient. DOM, Bluetooth and device service
// are simulated. These tests do not establish rendering or Android behavior.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {WifiSetupClient,WifiInputError} from '../../phone/wifi-setup/connection.mjs';
function fixture({protocol='https:',delayConnect=false,wrongWifi=false,scanLocalOnly=false,deviceId='CSP-W2-SYNTHETIC'}={}){
 class Element{hidden=false;disabled=false;checked=false;textContent='';value='';className='';children=[];
  append(child){this.children.push(child);}replaceChildren(...children){this.children=children;}}
 const ids=[...readFileSync(new URL('../../phone/wifi-setup/index.html',import.meta.url),'utf8').matchAll(/\bid="([^"]+)"/g)].map(match=>match[1]);
 const elements=Object.fromEntries(ids.map(id=>[id,new Element()]));
 for(const id of ['wifi-card','candidate-card','save-panel','disconnect','saved-message'])elements[id].hidden=true;
 const windowEvents=new Map(),documentEvents=new Map();
 const document={visibilityState:'visible',getElementById:id=>elements[id],createElement:()=>new Element(),createTextNode:text=>({textContent:text}),
  addEventListener:(name,fn)=>documentEvents.set(name,fn)};
 let made=0,releaseConnect,commits=0,staged=null,revision='1',transaction=null,options,commands=0;
 class Connection{
  constructor(opts){made++;options=opts;assert(elements['owner-key'].value==='');}
  async connect(){options.onState({state:'choosing',message:'Choose device'});if(delayConnect)await new Promise(resolve=>releaseConnect=resolve);
   const info={device_id:deviceId,firmware:'SYNTHETIC'};options.onState({state:'connected',message:'Owner connected',info});return info;}
  disconnect(){options?.onState({state:'disconnected',message:'Closed'});}
  async command(op,args={}){
   commands++;
   if(op==='list')return {ok:true,revision,count:0,profiles:[],next:0};
   if(op==='scan')return {ok:true};
   if(op==='scan_page')return {ok:true,wifi:scanLocalOnly?'local_connected':'idle',has_scan:true,error:scanLocalOnly?20:0,next:0,networks:[{ssid:'CSP Test <img>',open:false,supported:true,rssi:-55}]};
   if(op==='begin'){transaction='1';return {ok:true,transaction,edit:0};}
   if(op==='stage'){staged=args;return {ok:true,profile:1,edit:1};}
   if(op==='check')return {ok:true};
   if(op==='status')return {ok:true,revision,durable:revision!=='1',store_fault:false,wifi:wrongWifi?'failed':'local_connected',wifi_error:wrongWifi?14:20,wifi_profile:1,selected_profile:1,applied_revision:revision,
    internet_verified:false,cloud_available:false,backups_paused:true,...(transaction?{transaction,edit:1,phase:wrongWifi?4:3,check_error:wrongWifi?14:20}:{})};
   if(op==='commit'){commits++;revision='2';transaction=null;return {ok:true,durable:true,revision,internet_verified:false};}
   if(op==='abort'){transaction=null;return {ok:true};}throw new Error('unexpected command');
  }
 }
 vm.runInNewContext(readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,''),
  {ManagementConnection:Connection,WifiSetupClient,WifiInputError,document,location:{protocol},isSecureContext:true,navigator:{bluetooth:{requestDevice(){}}},
   TextEncoder,addEventListener:(name,fn)=>windowEvents.set(name,fn)});
 const event=name=>(windowEvents.get(name)||documentEvents.get(name))?.({persisted:true});
 return {e:elements,document,event,get commits(){return commits;},get staged(){return staged;},get made(){return made;},get commands(){return commands;},release:()=>releaseConnect()};
}
async function connected(f){f.e['owner-key'].value='PUBLIC_SYNTHETIC_OWNER_KEY_ONLY';await f.e.connect.onclick();assert(!f.e['wifi-card'].hidden);}
async function candidate(f){await f.e.scan.onclick();const button=f.e.networks.children[0].children[0];assert.equal(button.children[0].textContent,'CSP Test <img>');await button.onclick();
 assert.equal(f.e['selected-name'].textContent,'CSP Test <img>');f.e['wifi-password'].value='public-synthetic-pass';await f.e.check.onclick();}
test('actual UI: secure connection, text-only scan, check, explicit consent and durable save',async()=>{
 const f=fixture();await connected(f);await candidate(f);assert(!f.e['save-panel'].hidden);assert.equal(f.e['wifi-password'].value,'');
 await f.e.save.onclick();assert.equal(f.commits,0); // consent not checked
 f.e['accept-local'].checked=true;f.e['accept-local'].onchange();await f.e.save.onclick();assert.equal(f.commits,1);
  assert(!f.e['saved-message'].hidden);assert.match(f.e['saved-message'].textContent,/saved on the dongle and read back successfully/);
  assert.match(f.e['saved-message'].textContent,/Internet and Koofr access still need a separate check/);
 assert.equal(f.e['owner-key'].value,'');assert(!Object.hasOwn(f.staged,'owner_key'));
});
test('wrong Wi-Fi error never enables save or claims internet readiness',async()=>{
 const f=fixture({wrongWifi:true});await connected(f);await candidate(f);
 assert(f.e['save-panel'].hidden);assert.match(f.e['check-message'].textContent,/password was rejected/);assert.equal(f.commits,0);
});
test('completed scan while locally connected lists networks instead of reporting failure20',async()=>{
 const f=fixture({scanLocalOnly:true});await connected(f);await f.e.scan.onclick();
 assert.equal(f.e.networks.children.length,1);assert.match(f.e['scan-message'].textContent,/Choose your network/);
 assert.equal(f.commits,0);assert(!f.e['wifi-card'].hidden);
});
for(const reason of ['disconnect','pagehide','freeze','visibilitychange','resume'])test(reason+' clears auth, passwords, identity and candidate',async()=>{
 const f=fixture();await connected(f);await candidate(f);f.e['owner-key'].value='PUBLIC_TEST_KEY';f.e['wifi-password'].value='PUBLIC_TEST_WIFI';
 if(reason==='disconnect')f.e.disconnect.onclick();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}
 assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');assert(f.e['wifi-card'].hidden);assert(f.e['candidate-card'].hidden);assert(f.e['save-panel'].hidden);
 assert.equal(f.e['owner-key'].value,'');assert.equal(f.e['wifi-password'].value,'');assert.equal(f.e.identity.textContent,'');
 f.document.visibilityState='visible';f.event('pageshow');assert(f.e['wifi-card'].hidden);assert.equal(f.made,1);
});
test('late connect after freeze cannot repaint authenticated UI',async()=>{
 const f=fixture({delayConnect:true});f.e['owner-key'].value='PUBLIC_SYNTHETIC_OWNER_KEY_ONLY';const pending=f.e.connect.onclick();
 f.event('freeze');f.release();await pending;assert(f.e['wifi-card'].hidden);assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');
});
test('HTTP preview refuses owner-key collection and Bluetooth use',async()=>{
 const f=fixture({protocol:'http:'});assert(f.e['owner-key'].disabled);assert(f.e.connect.disabled);
 await f.e.connect.onclick();assert.equal(f.made,0);assert.match(f.e['connection-message'].textContent,/HTTPS/);
});
test('private-key reveal is explicit, unavailable on HTTP and reset on disconnect',async()=>{
 const f=fixture();f.e['owner-key'].value='PUBLIC_SYNTHETIC_KEY';
 f.e['show-owner-key'].checked=true;f.e['show-owner-key'].onchange();assert.equal(f.e['owner-key'].type,'text');
 f.e['show-owner-key'].checked=false;f.e['show-owner-key'].onchange();assert.equal(f.e['owner-key'].type,'password');
 f.e['show-owner-key'].checked=true;f.e['show-owner-key'].onchange();await connected(f);
 assert(f.e['show-owner-key'].disabled);assert.equal(f.e['owner-key'].type,'password');assert.equal(f.e['owner-key'].value,'');
 f.e.disconnect.onclick();assert.equal(f.e['show-owner-key'].checked,false);assert.equal(f.e['owner-key'].type,'password');
 const http=fixture({protocol:'http:'});http.e['show-owner-key'].checked=true;http.e['show-owner-key'].onchange();
 assert(http.e['show-owner-key'].disabled);assert.equal(http.e['owner-key'].type,'password');
});
test('Wi-Fi reveal clears on saved-password reuse and page freeze',async()=>{
 const f=fixture();await connected(f);await candidate(f);
 f.e['keep-secret'].checked=false;f.e['keep-secret'].onchange();f.e['wifi-password'].value='PUBLIC_SYNTHETIC_PASSWORD';
 f.e['show-wifi-password'].checked=true;f.e['show-wifi-password'].onchange();assert.equal(f.e['wifi-password'].type,'text');
 f.e['keep-secret'].checked=true;f.e['keep-secret'].onchange();assert.equal(f.e['wifi-password'].value,'');assert.equal(f.e['wifi-password'].type,'password');
 assert.equal(f.e['show-wifi-password'].checked,false);
 f.e['keep-secret'].checked=false;f.e['keep-secret'].onchange();f.e['show-wifi-password'].checked=true;f.e['show-wifi-password'].onchange();
 f.event('freeze');assert.equal(f.e['wifi-password'].type,'password');assert.equal(f.e['show-wifi-password'].checked,false);
});
// Public fixtures only: these values never authenticate a physical device.
const publicCard={version:2,device:'001122334455',username:'csp-owner',owner_key:'PUBLIC_UI_KEY_'.padEnd(32,'X'),synthetic:false,record_sha256:'0'.repeat(64)};
assert.equal(publicCard.owner_key.length,32);
function cardFile(f,value=publicCard){const text=JSON.stringify(value);f.e['owner-card'].files=[{size:new TextEncoder().encode(text).length,text:async()=>text}];}
test('local private card fills key, binds authenticated identity and clears after connect',async()=>{
 const f=fixture({deviceId:'CSP-W2-'+publicCard.device});cardFile(f);await f.e['owner-card'].onchange();
 assert.equal(f.e['owner-key'].value,publicCard.owner_key);assert.equal(f.e['owner-card'].value,'');
 assert.match(f.e['owner-card-message'].textContent,/loaded locally/);await f.e.connect.onclick();
 assert(!f.e['wifi-card'].hidden);assert.equal(f.e['owner-key'].value,'');assert.equal(f.made,1);
});
test('card identity mismatch closes before Wi-Fi commands',async()=>{
 const f=fixture();cardFile(f);await f.e['owner-card'].onchange();await f.e.connect.onclick();
 assert(f.e['wifi-card'].hidden);assert.equal(f.commands,0);assert.match(f.e['connection-message'].textContent,/different Cloud Stick/);
 assert.equal(f.e['owner-key'].value,'');
});
for(const [name,change] of [['synthetic',{synthetic:true}],['unknown field',{url:'https://invalid.test'}],['wrong key',{owner_key:'short'}],['wrong device',{device:'<script>'}],['wrong version',{version:1}]])
 test('rejects '+name+' card without reflecting contents',async()=>{
  const f=fixture();cardFile(f,{...publicCard,...change});await f.e['owner-card'].onchange();
  assert.equal(f.e['owner-key'].value,'');assert.equal(f.made,0);assert.match(f.e['owner-card-message'].textContent,/not a supported/);
  assert(!f.e['owner-card-message'].textContent.includes(publicCard.owner_key));
 });
test('oversized card is rejected before reading file contents',async()=>{
 const f=fixture();let reads=0;f.e['owner-card'].files=[{size:2049,text:async()=>{reads++;return JSON.stringify(publicCard);}}];
 await f.e['owner-card'].onchange();assert.equal(reads,0);assert.equal(f.e['owner-key'].value,'');
});
test('late private card file read after freeze cannot restore a key',async()=>{
 const f=fixture();let release;f.e['owner-card'].files=[{size:512,text:()=>new Promise(resolve=>release=resolve)}];
 const pending=f.e['owner-card'].onchange();f.event('freeze');release(JSON.stringify(publicCard));await pending;
 assert.equal(f.e['owner-key'].value,'');assert(f.e['wifi-card'].hidden);assert.equal(f.made,0);
});
test('HTTP refuses even local owner-card reads',async()=>{
 const f=fixture({protocol:'http:'});let reads=0;f.e['owner-card'].files=[{size:512,text:async()=>{reads++;return JSON.stringify(publicCard);}}];
 assert(f.e['owner-card'].disabled);await f.e['owner-card'].onchange();assert.equal(reads,0);assert.equal(f.e['owner-key'].value,'');
});
