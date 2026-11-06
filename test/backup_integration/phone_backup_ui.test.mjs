// Actual page script and status parser. DOM, Bluetooth and device replies are
// simulated; no browser rendering, physical radio or cloud access is claimed.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {WifiSetupClient as ActualWifiSetupClient,WifiInputError} from '../../phone/wifi-setup/connection.mjs';

const html=readFileSync(new URL('../../phone/wifi-setup/index.html',import.meta.url),'utf8');
const source=readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,'');
const fields=['backup-session','backup-cloud','backup-last','backup-verified','backup-pending','backup-failed','backup-error'];
const backup={cloud_configured:true,last_backup_unix:1793793600,verified:12,pending:2,failed:0,backup_error:0,backups_paused:true};
const base={ok:true,revision:'6',durable:true,store_fault:false,wifi:'local_connected',wifi_error:20,wifi_profile:1,selected_profile:1,
 applied_revision:'6',internet_verified:false,cloud_available:false,backups_paused:true};

function fixture({snapshot=backup,capable=true,featureValue=true,delayedStatus=false,scanError=0,lateCheck=false}={}){
 class Element {
  hidden=false;disabled=false;checked=false;textContent='';value='';children=[];
  append(child){this.children.push(child);}replaceChildren(...children){this.children=children;}
  set innerHTML(_){throw new Error('Unsafe markup rendering');}
 }
 const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(match=>match[1]);
 const e=Object.fromEntries(ids.map(id=>[id,new Element()]));
 for(const id of ['backup-card','wifi-card','candidate-card','save-panel','disconnect','saved-message'])e[id].hidden=true;
 const windowEvents=new Map(),documentEvents=new Map();
 const document={visibilityState:'visible',getElementById:id=>e[id],createElement:()=>new Element(),createTextNode:text=>({textContent:text}),
  addEventListener:(name,handler)=>documentEvents.set(name,handler)};
 let options,statusRelease,checkRelease,statusRequests=0,disconnects=0;
 class Connection {
  constructor(next){options=next;}
  async connect(){const info={device_id:'CSP-W2-SYNTHETIC',firmware:'SYNTHETIC-COMBINED',...(capable?{backup_status:featureValue}:{})};options.onState({state:'connected',message:'Authenticated',info});return info;}
  disconnect(){++disconnects;options?.onState({state:'disconnected',message:'Disconnected'});}
 }
 class WifiSetupClient {
  constructor(){}
  async profiles(){return [];}
  async status(){return {...base};}
  async backupStatus(){++statusRequests;if(delayedStatus&&statusRequests===1)await new Promise(resolve=>statusRelease=resolve);return {ok:true,...snapshot};}
  async scan(){if(scanError)throw new WifiInputError(scanError);return [];}
  async abort(){}
  async stage(){}
  async check({onStatus}){if(lateCheck)await new Promise(resolve=>checkRelease=resolve);onStatus({...backup,verified:29});}
 }
 vm.runInNewContext(source,{ManagementConnection:Connection,WifiSetupClient,WifiInputError,document,
  location:{protocol:'https:'},isSecureContext:true,navigator:{bluetooth:{requestDevice(){}}},TextEncoder,
  addEventListener:(name,handler)=>windowEvents.set(name,handler)});
 const event=(name,event={persisted:true})=>(windowEvents.get(name)||documentEvents.get(name))?.(event);
 return {e,document,event,notify:state=>options.onState(state),releaseStatus:()=>statusRelease(),releaseCheck:()=>checkRelease(),
  get hasPendingStatus(){return !!statusRelease;},get hasPendingCheck(){return !!checkRelease;},get disconnects(){return disconnects;},get backupRequests(){return statusRequests;}};
}
async function connect(f){f.e['owner-key'].value='PUBLIC_SYNTHETIC_KEY_ONLY';await f.e.connect.onclick();}
function cleared(f){assert(f.e['backup-card'].hidden);for(const id of fields)assert.equal(f.e[id].textContent,'',id);}
async function until(predicate){for(let i=0;i<30&&!predicate();++i)await Promise.resolve();assert(predicate());}

test('complete authenticated snapshot shows only bounded recorded facts',async()=>{
 const f=fixture();await connect(f);assert(!f.e['backup-card'].hidden);
 assert.equal(f.e['backup-verified'].textContent,'12');assert.equal(f.e['backup-pending'].textContent,'2');assert.equal(f.e['backup-failed'].textContent,'0');
 assert.match(f.e['backup-session'].textContent,/paused during this setup session/);
 assert.match(f.e['backup-cloud'].textContent,/Saved on the stick; access is checked separately/);
 assert.match(f.e['backup-error'].textContent,/No backup error reported/);assert(f.e['backup-last'].textContent);
 assert(!fields.some(id=>/authenticated|uploading|resumed|synthetic-secret/.test(f.e[id].textContent)));
 assert.match(html,/not a live upload or a fresh cloud check/);assert.match(html,/checks whether it can resume/);
});
test('existing setup-only firmware keeps optional backup card hidden',async()=>{
 const f=fixture({capable:false});await connect(f);assert(!f.e['wifi-card'].hidden);assert.equal(f.backupRequests,0);cleared(f);
});
for(const featureValue of [false,'true',1])test('UI does not query backups for capability '+JSON.stringify(featureValue),async()=>{
 const f=fixture({featureValue});await connect(f);assert.equal(f.backupRequests,0);cleared(f);
});
test('empty backup history and unconfigured cloud make no success claim',async()=>{
 const f=fixture({snapshot:{...backup,last_backup_unix:0,cloud_configured:false,verified:0,pending:0}});await connect(f);
 assert.equal(f.e['backup-last'].textContent,'No verified backup record reported yet.');assert.equal(f.e['backup-cloud'].textContent,'Cloud setup is needed.');
});
test('backup errors show only a numeric code without file names or credentials',async()=>{
 const f=fixture({snapshot:{...backup,failed:2,backup_error:-2001}});await connect(f);
 assert.match(f.e['backup-error'].textContent,/Device code: -2001/);assert(!f.e['backup-error'].textContent.includes('http'));
});
for(const [name,snapshot] of [
 ['partial',{cloud_configured:true}],['timestamp string',{...backup,last_backup_unix:'1793793600'}],
 ['invalid date',{...backup,last_backup_unix:253402300800}],['negative count',{...backup,verified:-1}],
 ['fraction count',{...backup,pending:0.5}],['overflow count',{...backup,failed:0x100000000}],
 ['error text',{...backup,backup_error:'synthetic-secret'}],['cloud text',{...backup,cloud_configured:'synthetic-secret'}],
])test('UI hides invalid '+name+' snapshot without reflecting it',async()=>{
 const f=fixture({snapshot});await connect(f);cleared(f);assert(!fields.some(id=>f.e[id].textContent.includes('synthetic-secret')));
});

for(const reason of ['disconnect','pagehide','pageshow','freeze','resume','visibilitychange'])test(reason+' scrubs all authenticated backup fields',async()=>{
 const f=fixture();await connect(f);assert(!f.e['backup-card'].hidden);
 if(reason==='disconnect')f.e.disconnect.onclick();else{if(reason==='visibilitychange')f.document.visibilityState='hidden';f.event(reason);}
 cleared(f);assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');
 f.document.visibilityState='visible';f.event('pageshow');cleared(f);
});
test('connection revocation scrubs backup state immediately',async()=>{
 const f=fixture();await connect(f);f.notify({state:'error',message:'Session revoked'});cleared(f);
 assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');
});
for(const code of [1,5,13])test('session error '+code+' closes UI instead of retaining recorded facts',async()=>{
 const f=fixture({scanError:code});await connect(f);await f.e.scan.onclick();cleared(f);
 assert.notEqual(f.e['connection-state'].textContent,'Private owner authenticated');assert(f.disconnects>0);
});
test('late authenticated status after page freeze cannot restore facts',async()=>{
 const f=fixture({delayedStatus:true});const pending=connect(f);await until(()=>f.hasPendingStatus);
 f.event('freeze');f.releaseStatus();await pending;cleared(f);
});
test('a late status from an older session cannot repaint a newer closed UI',async()=>{
 const f=fixture({delayedStatus:true});const pending=connect(f);await until(()=>f.hasPendingStatus);
 f.e.disconnect.onclick();await connect(f);assert(!f.e['backup-card'].hidden);
 f.e.disconnect.onclick();f.releaseStatus();await pending;cleared(f);
});
test('late Wi-Fi check status cannot revive backup history after disconnect',async()=>{
 const f=fixture({lateCheck:true});await connect(f);f.e['manual-ssid'].value='SYNTHETIC';f.e['manual-open'].checked=true;
 await f.e.manual.onclick();const pending=f.e.check.onclick();await until(()=>f.hasPendingCheck);
 f.e.disconnect.onclick();f.releaseCheck();await pending;cleared(f);
});

function parser(snapshot){let disconnected=0;const flow=new ActualWifiSetupClient({command:async op=>{assert.equal(op,'backup_status');return snapshot;},disconnect:()=>++disconnected});
 return {flow,get disconnected(){return disconnected;}};}
test('actual protocol accepts complete bounded dedicated backup snapshot',async()=>{
 const full=parser({ok:true,...backup});assert.equal((await full.flow.backupStatus()).verified,12);assert.equal(full.disconnected,0);
});
for(const [name,change] of [
 ['partial',{cloud_configured:true}],['string time',{...backup,last_backup_unix:'1'}],['negative time',{...backup,last_backup_unix:-1}],
 ['unsafe date',{...backup,last_backup_unix:253402300800}],['NaN',{...backup,last_backup_unix:NaN}],
 ['negative verified',{...backup,verified:-1}],['fraction pending',{...backup,pending:1.5}],['count overflow',{...backup,failed:0x100000000}],
 ['low error',{...backup,backup_error:-0x80000001}],['high error',{...backup,backup_error:0x80000000}],
 ['nonboolean cloud',{...backup,cloud_configured:1}],['unpaused worker',{...backup,backups_paused:false}],
 ['filename field',{...backup,current_file:'PRIVATE_NAME_MUST_NOT_RENDER.pdf'}],
])test('actual protocol rejects '+name+' backup status and disconnects',async()=>{
 const f=parser({ok:true,...change});await assert.rejects(f.flow.backupStatus());assert.equal(f.disconnected,1);
});
