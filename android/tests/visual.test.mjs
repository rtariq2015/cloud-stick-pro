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
 assert.deepEqual([...ids].sort(),['connection-badge','login','login-form','username','password','connect','chooser','devices','cancel','message','result','result-title','snapshot-read','card-verified','count-verified','card-pending','count-pending','card-failed','count-failed','backup-attention','status','history-note','history','session-actions','session-hint','refresh','done'].sort());
 for(const id of ['chooser','result'])assert.match(html,new RegExp('<section\\b[^>]*id="'+id+'"[^>]*\\bhidden\\b'));
 assert.match(html,/<aside\b[^>]*id="session-actions"[^>]*\bhidden\b/);
 assert.match(css,/\[hidden\]\s*\{\s*display:\s*none\s*!important/);
 for(const id of ['history','status','count-verified','count-pending','count-failed']){
  const position=html.indexOf('id="'+id+'"');
  assert.ok(position>html.indexOf('id="result"')&&position<html.indexOf('id="session-actions"'));
 }
});

test('collapsed secondary details retain visible connection guidance and honest Done action',()=>{
 assert.match(html,/<details class="backup-details card"|<details class="card backup-details">/);
 assert.doesNotMatch(html,/<details\b[^>]*\bopen(?:\s|>|=)/);
 assert.ok(html.indexOf('Hold BOOT for 3 seconds')<html.indexOf('id="login-form"'));
 assert.match(html,/Recorded history, not live cloud monitoring/);
 assert.match(html,/Done closes Bluetooth; the stick decides when it can resume/);
 assert.match(html,/<button id="done" type="button">Done<span class="button-caption">Close Bluetooth/);
 assert.match(html,/<label for="username">Username<\/label>/);
 assert.match(html,/<label for="password">Password<\/label>/);
 assert.match(html,/id="password"[^>]*type="password"[^>]*autocomplete="off"/);
 assert.doesNotMatch(html,/<(?:iframe|object|embed)\b|https?:\/\/|\son[a-z]+\s*=/i);
});

test('presentation-only revision leaves app lifecycle, native bridge and secured modules byte-exact',()=>{
 const expected={
  'app.mjs':'f71cc0c22267869bd38d4dc9b6a2fdb213e29058ef75ba29280e3a890690606a',
  'native.mjs':'8bf45bd119e83f272089838b4b7bcd04a90dcfcecde2daff3f368b4566bbaa19',
  'shared/connection.mjs':'79d12aa9adc562649d6048ca6b151400213e9d65b8f98bc0f906f729e08cd869',
  'shared/security2.mjs':'ee4e2ec460eaf3a1bc20f5ffe0abea393eba9b2cde14f59a0a24be840202fc66',
 };
 for(const [name,sha] of Object.entries(expected))assert.equal(createHash('sha256').update(readFileSync(new URL(name,base))).digest('hex'),sha,name);
});
