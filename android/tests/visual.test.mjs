import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';

const base=new URL('../app/src/main/assets/',import.meta.url);
const html=readFileSync(new URL('index.html',base),'utf8');
const css=readFileSync(new URL('style.css',base),'utf8');
const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(match=>match[1]);

test('visual revision preserves every viewer hook and unauthenticated privacy boundary',()=>{
 assert.equal(new Set(ids).size,ids.length,'DOM IDs must remain unique');
 assert.deepEqual([...ids].filter(id=>!id.startsWith('live-')&&!id.startsWith('settings-')&&!id.startsWith('app-update-')&&!['stick-settings','find-address','found-addresses'].includes(id)&&!['dashboard-badge','dashboard-tab','private-tab','dashboard-pane','dashboard-data','private-pane','address-details','saved-address','address-form','dashboard-address','save-address','forget-address','address-message'].includes(id)).sort(),['manual-connect','trust-pairing','trust-phone','pairing-message','connection-badge','login','login-form','username','password','connect','chooser','devices','cancel','message','result','result-title','snapshot-read','card-verified','count-verified','card-pending','count-pending','card-failed','count-failed','backup-attention','status','history-note','history','session-actions','session-hint','refresh','done'].sort());
 for(const id of ['chooser','result','stick-settings'])assert.match(html,new RegExp('<section\\b[^>]*id="'+id+'"[^>]*\\bhidden\\b'));
 assert.match(html,/<aside\b[^>]*id="session-actions"[^>]*\bhidden\b/);
 assert.match(css,/\[hidden\]\s*\{\s*display:\s*none\s*!important/);
 for(const id of ['history','status','count-verified','count-pending','count-failed']){
  const position=html.indexOf('id="'+id+'"');
  assert.ok(position>html.indexOf('id="result"')&&position<html.indexOf('id="session-actions"'));
 }
});

test('collapsed secondary details retain visible connection guidance and honest Done action',()=>{
 assert.match(html,/<details class="backup-details card"|<details class="card backup-details">/);
 assert.doesNotMatch(html,/<details\b[^>]*class="(?:card backup-details|backup-details card)"[^>]*\bopen(?:\s|>|=)/);
 assert.ok(html.indexOf('First connection or no Wi-Fi?')<html.indexOf('id="login-form"'));
 assert.match(html,/id="manual-connect" type="checkbox"/);
 assert.match(html,/id="trust-pairing"[^>]*hidden/);
 assert.match(html,/address and trusted-phone pairing are remembered/);
 assert.match(html,/Done closes Bluetooth; the stick decides when it can resume/);
 assert.match(html,/<button id="done" type="button">Done<span class="button-caption">Close Bluetooth/);
 assert.match(html,/<label for="username">Username<\/label>/);
 assert.match(html,/<label for="password">Password<\/label>/);
 assert.match(html,/id="password"[^>]*type="password"[^>]*autocomplete="off"/);
 assert.doesNotMatch(html,/<(?:iframe|object|embed)\b|https?:\/\/|\son[a-z]+\s*=/i);
});

test('Wake revision pins reviewed shared capability changes and unchanged authentication',()=>{
 const expected={
  'shared/connection.mjs':'f4530bdfb8ae6669fd48ea6a3efabbbec96bdc55fbc3585f185bfa3cab103b2e',
  'shared/security2.mjs':'ee4e2ec460eaf3a1bc20f5ffe0abea393eba9b2cde14f59a0a24be840202fc66',
 };
 for(const [name,sha] of Object.entries(expected))assert.equal(createHash('sha256').update(readFileSync(new URL(name,base))).digest('hex'),sha,name);
});
