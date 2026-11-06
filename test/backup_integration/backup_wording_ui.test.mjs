// Execute the actual page script. DOM/Bluetooth replies are simulated; no page
// is deployed and no real GATT/cloud operation is performed.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {WifiInputError} from '../../phone/wifi-setup/connection.mjs';
const html=readFileSync(new URL('../../phone/wifi-setup/index.html',import.meta.url),'utf8');
const source=readFileSync(new URL('../../phone/wifi-setup/app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,'');
const recorded={cloud_configured:true,last_backup_unix:1793793600,verified:12,pending:2,failed:0,backup_error:0,backups_paused:true};
async function paint(change={}){
 class Element{
  value='';textContent='';hidden=false;disabled=false;checked=false;
  replaceChildren(){}append(){}set innerHTML(_){throw Error('Unsafe rendering');}
 }
 const elements=Object.fromEntries([...html.matchAll(/\bid="([^"]+)"/g)].map(match=>[match[1],new Element()]));
 let onState;
 class ManagementConnection{
  constructor(options){onState=options.onState;}
  async connect(){const info={device_id:'SYNTHETIC',firmware:'SYNTHETIC',backup_status:true};onState({state:'connected',message:'Authenticated',info});return info;}
  disconnect(){onState?.({state:'disconnected',message:'Disconnected'});}
 }
 class WifiSetupClient{
  async profiles(){return [];}
  async status(){return {selected_profile:0};}
  async backupStatus(){return {ok:true,...recorded,...change};}
 }
 vm.runInNewContext(source,{ManagementConnection,WifiSetupClient,WifiInputError,
  document:{visibilityState:'visible',getElementById:id=>elements[id],addEventListener(){}},
  location:{protocol:'https:'},isSecureContext:true,navigator:{bluetooth:{requestDevice(){}}},
  TextEncoder,addEventListener(){}});
 elements['owner-key'].value='SYNTHETIC_KEY_ONLY';await elements.connect.onclick();
 return elements;
}
const cases=[
 [-2303,/Waiting for network time.*correct clock.*secure cloud connection/],
 [-2302,/Wi-Fi was disconnected.*saved networks/],
 [-2301,/backup service is not running/i],
 [401,/Koofr rejected.*email and app password/],
 [403,/Koofr refused.*folder permissions/],
 [507,/Cloud storage is full.*quota/],
 [-2001,/secure cloud connection could not start.*network time/],
 [-2002,/did not match.*another content check/],
 [-2003,/SD card could not be read.*Safely eject/],
 [-2105,/SD card could not be read/],[-2109,/SD card could not be read/],
 [-2101,/nested too deeply/],[-2102,/backup folder could not be opened/i],
 [-2103,/too many directory entries/],[-2104,/name is unsupported/],[-2106,/128-file limit/],
 [-2107,/exceeds.*upload size limit/],
 [-2108,/internal backup records are unavailable or full/],[-2111,/internal backup records/],[-2112,/internal backup records/],
 [-2110,/record needed repair.*content check/],
 [-2113,/working memory.*Backups stay paused/],[-2115,/could not close.*Setup stays blocked/],
 [-2010,/paused for setup or a new USB copy.*another check/],
 [-2011,/folder response could not be used/],[-2012,/transfer did not finish/i],
];
for(const [code,help] of cases)test('actual phone explains backup code '+code+' without changing recorded facts',async()=>{
 const e=await paint({backup_error:code,failed:2});assert.equal(e['backup-card'].hidden,false);
 assert.match(e['backup-error'].textContent,help);assert.match(e['backup-error'].textContent,new RegExp('Device code: '+code+'\\.'));
 assert.match(e['backup-error'].textContent,/Original files stay on the USB/);
 assert.equal(e['backup-verified'].textContent,'12');assert.equal(e['backup-pending'].textContent,'2');assert.equal(e['backup-failed'].textContent,'2');
 assert.match(e['backup-cloud'].textContent,/access is checked separately/);
 assert(!/No backup error reported|all files.*verified|backup succeeded|cloud connected/i.test(e['backup-error'].textContent));
});
for(const code of [-9999,412,-0x80000000,0x7fffffff])test('unknown backup code '+code+' retains an actionable numeric diagnosis',async()=>{
 const e=await paint({backup_error:code});assert.match(e['backup-error'].textContent,/Check the stick's status before retrying/);
 assert(e['backup-error'].textContent.includes(String(code)));assert(!e['backup-error'].textContent.includes('http'));
});
test('network-time wait does not turn recorded counts/date into fresh cloud success',async()=>{
 const e=await paint({backup_error:-2303,last_backup_unix:0});
 assert.equal(e['backup-last'].textContent,'No verified backup record reported yet.');
 assert.equal(e['backup-verified'].textContent,'12');assert.match(e['backup-error'].textContent,/Waiting for network time/);
 assert.match(e['backup-session'].textContent,/paused during this setup session/);
 assert.match(e['backup-cloud'].textContent,/access is checked separately/);
});
test('pending or failed files have no all-files-complete wording',async()=>{
 for(const change of [{pending:2,failed:0},{pending:0,failed:2}]){
  const e=await paint(change);assert(!/all files|backed up successfully|cloud connected/i.test(e['backup-error'].textContent));
  assert.match(e['backup-error'].textContent,change.failed?/not verified/:/still need verification/);
 }
});
test('invalid text cannot enter the error wording or keep authenticated backup facts visible',async()=>{
 const e=await paint({backup_error:'PRIVATE_SSID_PASSWORD_FILENAME'});assert.equal(e['backup-card'].hidden,true);
 assert.equal(e['backup-error'].textContent,'');
});
