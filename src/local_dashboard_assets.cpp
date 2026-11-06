#include "local_dashboard.h"
namespace pro::local_dashboard {
const char Page[]=R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cloud Stick Pro · Dashboard</title>
<style>
:root{color-scheme:light;--ink:#153b35;--green:#17665a;--muted:#526962;--paper:#f3f5ee;--line:#d5e0d8}*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:17px/1.6 system-ui,sans-serif}main{max-width:900px;margin:auto;padding:40px 24px 70px}header{display:flex;justify-content:space-between;gap:20px;align-items:center;border-bottom:1px solid var(--line);padding-bottom:24px}a{color:var(--green);text-underline-offset:4px}.brand{font-weight:750;font-size:20px}.eyebrow{font-size:12px;letter-spacing:.15em;text-transform:uppercase;font-weight:700;color:var(--green)}h1{font-size:clamp(34px,6vw,52px);line-height:1.12;letter-spacing:-.04em;margin:16px 0}h2{font-size:22px;margin:0 0 12px}p{color:var(--muted)}.hero{padding:32px 0 24px}.badge{border:1px solid var(--line);border-radius:99px;padding:5px 12px;font-size:13px}.card{background:#fffef8;border:1px solid var(--line);border-radius:24px;padding:26px;margin-bottom:18px}.counts{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}.counts .card{padding:20px;margin:0}.number{font-size:36px;font-weight:650;letter-spacing:-.04em}.label{font-size:14px;color:var(--muted)}progress{width:100%;height:18px;accent-color:var(--green)}.row{display:flex;justify-content:space-between;gap:20px;margin:10px 0}.row span:last-child{text-align:right}small{color:var(--muted)}.notice{border-left:3px solid #b99b50;padding-left:18px}footer{border-top:1px solid var(--line);padding-top:18px;font-size:13px;color:var(--muted)}@media(max-width:520px){main{padding:24px 18px}.card{padding:20px}.counts .card{padding:14px}.number{font-size:30px}header{align-items:flex-start;flex-direction:column;gap:8px}}
</style>
<main><header><span class="brand">Cloud Stick Pro</span><span id="live" class="badge" role="status">Connecting to your USB…</span></header>
<section class="hero"><div class="eyebrow">Your USB · Live dashboard</div><h1 id="headline">Connecting to your USB.</h1><p id="detail" aria-live="polite">Waiting for the USB to report its current status.</p></section>
<section class="counts" aria-label="Latest backup scan"><div class="card"><div id="verified" class="number">—</div><div class="label">Verified</div></div><div class="card"><div id="pending" class="number">—</div><div class="label">Pending</div></div><div class="card"><div id="failed" class="number">—</div><div class="label">Need attention</div></div></section>
<p id="scan">Counts appear after the first scan.</p>
<section class="card"><h2 id="action">Current activity</h2><progress id="progress" max="100" aria-label="Current transfer progress"></progress><div class="row"><span id="bytes">Waiting for status</span><span id="speed">—</span></div><small id="last">No verified backup reported yet.</small></section>
<section class="card"><h2>Device connection</h2><div class="row"><span>Wi-Fi</span><span id="wifi">—</span></div><div class="row"><span>microSD</span><span id="sd">—</span></div><div class="row"><span>Firmware</span><span id="version">—</span></div><div class="row"><span>Running since restart</span><span id="uptime">—</span></div></section>
<section class="card"><h2>Set up your USB</h2><p>Use the secure setup page to choose Wi-Fi and view private file history. Sign in with your chosen username and password, or use your existing private key. Hold BOOT for 3 seconds to connect.</p><p><a href="https://cloud-stick-pro.rtariq.chatgpt.site/" target="_blank" rel="noopener noreferrer">Open Cloud Stick Pro setup ↗</a></p><p class="notice">This local page only shows device status. Never enter passwords or your owner key here. The dashboard pauses during Bluetooth setup and public Wi-Fi sign-in; it reconnects when normal backups resume.</p></section>
<footer><span id="updated">Waiting for a live update</span> · Updates every 3 seconds while this page is visible. Devices must share a reachable Wi-Fi network; some guest networks block access between devices.</footer></main>
 <script src="/app.js" defer></script></html>)HTML";
const size_t PageSize=sizeof Page-1;
const char Script[]=R"JS('use strict';
const byId=id=>document.getElementById(id);
const text=(id,value)=>{byId(id).textContent=value;};
let timer=0,current=null,epoch=0;
const amount=n=>n>=1048576?(n/1048576).toFixed(1)+' MiB':n>=1024?(n/1024).toFixed(1)+' KiB':n+' B';
function render(s){
 if(typeof s.firmware!=='string'||typeof s.wifi!=='boolean'||!Number.isInteger(s.uptime_ms))throw Error('Invalid device status');
 const error=Number(s.error)||0,transferring=s.active===true&&s.phase>=1&&s.phase<=4,scanning=s.active===true&&s.scanning===true;
 const phases=['Waiting','Checking a USB file','Uploading to Koofr','Verifying the cloud copy','Publishing verified copy'];
 const phase=phases[s.phase]||'Working';
 let title='Ready for new files',detail='Leave the USB plugged in. New files are backed up after the configured quiet period.';
 if(!s.media){title='Insert your microSD';detail='The USB has no initialized storage. Existing backup counts do not mean files are currently available.';}
 else if(!s.wifi){title='Waiting for Wi-Fi';detail='The USB will retry its saved network. Your files remain on the microSD.';}
 else if(!s.configured){title='Cloud setup needed';detail='Automatic backups cannot start until valid cloud settings are available. Your files stay on the USB.';}
 else if(transferring){title=phase;detail=s.phase===1?'Reading the local file to identify its contents. This is not another upload.':s.phase===3?'Reading back the cloud copy and comparing it with the original. This confirms a complete backup.':s.phase===4?'The checked cloud copy is being published before success is recorded.':'The original file stays on your USB. Success is recorded after the cloud copy is verified.';}
 else if(error===-2303){title='Waiting for network time';detail='A valid clock is required before a secure cloud connection can start.';}
 else if(error){title='Backup needs attention';detail='Code '+error+'. The USB keeps the original files and retries recoverable errors.';}
 else if(s.paused){title='Backups paused';detail='The USB is waiting for setup or maintenance to finish.';}
 else if(!s.window_open){title='Waiting for your schedule';detail='The next automatic backup will run during your saved time window.';}
 else if(scanning){title='Finding new or changed files';detail='Comparing the USB files with saved backup records. No file transfer is running.';}
 else if(s.active){title='Preparing the next backup';detail='The backup worker is active; no file transfer has started yet.';}
 else if(s.quiet_seconds){title='Waiting for file activity to settle';detail='Backup can start in '+s.quiet_seconds+' seconds if the USB stays idle.';}
 else if(s.scan_complete&&s.verified&&!s.pending&&!s.failed){title='Your files are verified';detail='The latest completed scan found no files waiting for backup.';}
 else if(s.failed||s.pending){title='Waiting to retry backup';detail='Some files still need a verified cloud copy. Their originals remain on the USB.';}
 else if(s.has_scan&&!s.scan_complete){title='Waiting to continue';detail='The previous scan did not finish. Its partial counts are not a new scan or upload.';}
 text('headline',title);text('detail',detail);text('action',transferring?phase:scanning?'Finding files':s.paused?'Backup paused':s.active?'Preparing backup':'No transfer running');
 text('live','Live · USB connected');text('version',s.firmware);text('wifi',s.wifi?'Connected':'Reconnecting');text('sd',s.media?'Ready':'Not ready');
 for(const key of ['verified','pending','failed'])text(key,s.has_scan?String(s[key]):'—');
 text('scan',!s.has_scan?scanning?'First scan in progress; counts are not ready yet.':'Waiting for the first scan.':scanning?'Scan in progress; counts are provisional.':s.scan_complete?'Counts from the latest completed scan.':'Partial counts from an incomplete scan; no scan is running now.');
 const bar=byId('progress');
 // Inactive progress fields are the last transfer's record, not a live action.
 const caughtUp=!s.active&&s.media&&s.wifi&&s.configured&&!error&&!s.paused&&s.window_open&&!s.quiet_seconds&&s.scan_complete&&!s.pending&&!s.failed&&s.verified;
 bar.hidden=!transferring&&!scanning&&!s.active&&!caughtUp;
 if(transferring&&s.phase!==4&&s.percent>=0)bar.value=Math.min(100,s.percent);else if(caughtUp)bar.value=100;else bar.removeAttribute('value');
 text('bytes',transferring?(s.phase===4?'Publishing the checked copy':amount(s.done)+' / '+amount(s.total)):scanning?'Comparing file records':s.paused?'Paused':s.active?'Preparing backup':'No transfer running');
 text('speed',transferring&&s.phase!==4?amount(s.bytes_per_second)+'/s':'—');
 text('last',s.last_backup_unix?'Last recorded verified backup: '+new Date(s.last_backup_unix*1000).toLocaleString():'No verified backup reported yet.');
 const sec=Math.floor(s.uptime_ms/1000);text('uptime',Math.floor(sec/3600)+'h '+Math.floor(sec%3600/60)+'m');
 text('updated','Live update '+new Date().toLocaleTimeString());
}
function stale(paused=false){
 text('live',paused?'Page paused':'Dashboard unavailable · retrying');
 text('headline',paused?'Dashboard paused':'Reconnecting to your USB');
 text('detail','Displayed counts are historical until a fresh status arrives. Your USB may still be backing up.');
 text('scan','Previous counts; a live scan has not been confirmed.');
 text('action','Waiting for live status');text('bytes','Transfer progress unavailable');text('speed','—');byId('progress').removeAttribute('value');byId('progress').hidden=true;
 text('updated','No current connection; displayed values may be out of date');
}
function cancel(){++epoch;clearTimeout(timer);current?.abort();current=null;}
async function poll(){
 if(document.hidden||current)return;
 const generation=epoch,controller=new AbortController();current=controller;
 const valid=()=>generation===epoch&&current===controller&&!document.hidden;
 const timeout=setTimeout(()=>controller.abort(),2500);
 try{const result=await fetch('/status',{cache:'no-store',credentials:'omit',redirect:'error',signal:controller.signal});if(!result.ok)throw Error('Status unavailable');const state=await result.json();if(valid())render(state);}
 catch{if(valid())stale();}finally{clearTimeout(timeout);if(current===controller){current=null;if(generation===epoch&&!document.hidden)timer=setTimeout(poll,3000);}}
}
document.addEventListener('visibilitychange',()=>{cancel();stale(document.hidden);if(!document.hidden)poll();});
window.addEventListener('pagehide',()=>{cancel();stale(true);});
window.addEventListener('pageshow',event=>{if(event.persisted){cancel();stale();poll();}});
poll();
)JS";
const size_t ScriptSize=sizeof Script-1;
}
