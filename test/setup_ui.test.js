'use strict';
// Exercise the actual firmware script without a browser, npm, or network access.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');

const source = fs.readFileSync(path.join(__dirname, '../include/portal_ui.h'), 'utf8');
const page = source.match(/Page\[\] PROGMEM=R"HTML\(([\s\S]*?)\)HTML";/)[1];
const script = source.match(/Script\[\] PROGMEM=R"JS\(([\s\S]*?)\)JS";/)[1];
const publicCA = '-----BEGIN CERTIFICATE-----\nFAKE-PUBLIC-TEST-CA\n-----END CERTIFICATE-----';
const presetURL = 'https://app.koofr.net/dav/Koofr/CloudStickPro/';

function fixture(overrides = {}) {
  const elements = new Map();
  const namedFields = new Map();
  const calls = [];
  const timeouts = new Map();
  const intervals = [];
  let timerID = 0;
  let now = 1000;
  let document;

  class Element {
    constructor(id = '', tag = '') {
      Object.assign(this, {id, tag, value: '', type: 'text', textContent: '',
        className: '', hidden: false, readOnly: false, disabled: false,
        checked: false, open: false, children: [], listeners: new Map()});
    }
    set innerHTML(_) { throw new Error('Unsafe HTML sink used for setup data'); }
    get innerHTML() { throw new Error('Setup must not depend on HTML serialization'); }
    addEventListener(name, callback) { this.listeners.set(name, callback); }
    appendChild(child) { this.children.push(child); return child; }
    replaceChildren(...children) { this.children = children; this.value = ''; }
    focus() { document.activeElement = this; }
  }

  // Derive controls and FormData names from the production page, not a second form.
  for (const match of page.matchAll(/\bid="([^"]+)"/g)) {
    elements.set(match[1], new Element(match[1]));
  }
  for (const match of page.matchAll(/<(input|select|textarea|button|a)\b([^>]*)>/g)) {
    const attributes = match[2];
    const attribute = name => attributes.match(new RegExp(`\\b${name}="([^"]*)"`))?.[1];
    const id = attribute('id');
    if (!id) continue;
    const element = elements.get(id);
    element.tag = match[1];
    element.type = attribute('type') || 'text';
    element.value = attribute('value') || '';
    element.disabled = /\bdisabled\b/.test(attributes);
    element.required = /\brequired\b/.test(attributes);
    if (attribute('href')) element.href = attribute('href');
    if (attribute('name')) namedFields.set(attribute('name'), element);
  }
  for (const match of page.matchAll(/<select\b[^>]*\bid="([^"]+)"[^>]*>([\s\S]*?)<\/select>/g)) {
    elements.get(match[1]).value = match[2].match(/<option\b[^>]*\bvalue="([^"]*)"/)?.[1] || '';
  }
  document = {
    activeElement: null,
    getElementById: id => elements.get(id) || null,
    createElement: tag => new Element('', tag),
  };
  class FormData {
    constructor(form) { assert.equal(form, elements.get('config')); }
    *[Symbol.iterator]() {
      for (const [name, element] of namedFields) {
        if (!element.disabled && (element.type !== 'checkbox' || element.checked)) yield [name, element.value];
      }
    }
  }
  class Clock extends Date { static now() { return now; } }
  const response = (body, status = 200) => ({
    ok: status >= 200 && status < 300, status,
    async json() { return JSON.parse(JSON.stringify(body)); },
    async text() { return typeof body === 'string' ? body : JSON.stringify(body); },
  });
  const status = {
    csrf: 'setup-csrf-token', firmware: 'test-firmware', storage: 'USB host owns storage',
    has_scan_since_boot: false, last_backup_unix: 0, message: 'Ready',
  };
  let connection = overrides.connection || {state: 'idle', message: 'Choose a network.'};
  const fetch = async (url, options) => {
    // No external origin, credentials in URLs, or query strings are acceptable.
    assert.ok(['/history', '/status', '/presets', '/wifi-scan', '/wifi-connect', '/wifi-disconnect', '/config', '/resume', '/cloud-check', '/cloud-test'].includes(url), url);
    calls.push({url, options: options ? JSON.parse(JSON.stringify(options)) : undefined});
    if (url === '/history') return response(overrides.history || {files:[],more:false,cursor:''});
    if (url === '/status') return response({...status,...overrides.status}, overrides.statusCode || 200);
    if (url === '/presets') {
      return response({koofr: {server_url: presetURL, ca_pem: publicCA},saved:overrides.saved}, overrides.presetsCode || 200);
    }
    if (url === '/wifi-scan') {
      if (options?.method === 'POST') return response(overrides.scanStart || {state: 'ready', networks: []}, overrides.scanCode || 200);
      return response(overrides.scanPoll || {status: 'ready', networks: []});
    }
    if (url === '/wifi-connect') {
      if (options?.method === 'POST') connection = overrides.connectResult || {state: 'connecting', message: 'Joining Wi-Fi…'};
      return response(connection, overrides.connectionCode || 200);
    }
    if (url === '/wifi-disconnect') {
      connection = {state: 'idle', message: 'Disconnected.'};
      return response(connection);
    }
    return response('Settings saved locally.', overrides.saveCode || 200);
  };
  const context = vm.createContext({
    document, FormData, fetch, Date: Clock,
    setTimeout(callback, delay) { const id = ++timerID; timeouts.set(id, {callback, delay}); return id; },
    clearTimeout(id) { timeouts.delete(id); },
    setInterval(callback, delay) { intervals.push({callback, delay}); return 10000 + intervals.length; },
  });
  vm.runInContext(script, context, {filename: 'embedded-setup.js'});
  return {
    el: id => elements.get(id), calls, timeouts, intervals, context,
    async ready() { for (let i = 0; i < 30; ++i) await Promise.resolve(); },
    async timer() {
      const entry = timeouts.entries().next().value;
      assert.ok(entry, 'Expected a scheduled scan poll');
      timeouts.delete(entry[0]);
      await entry[1].callback();
    },
    advance(ms) { now += ms; },
    async submit() {
      let prevented = false;
      await elements.get('config').listeners.get('submit')({
        target: elements.get('config'), preventDefault() { prevented = true; },
      });
      assert.ok(prevented, 'Submission must stay inside the local setup page');
    },
  };
}

test('startup fills Koofr defaults and searches Wi-Fi automatically without account secrets', async () => {
  const ui = fixture(); await ui.ready();
  assert.equal(ui.el('server-url').value, presetURL);
  assert.equal(ui.el('ca-pem').value, publicCA);
  assert.equal(ui.el('server-url').readOnly, true);
  assert.equal(ui.el('ca-pem').readOnly, true);
  assert.equal(ui.el('username').value, '');
  assert.equal(ui.el('cloud-password').value, '');
  assert.equal(ui.el('save').disabled, false);
  assert.deepEqual(ui.calls.map(call => call.url), ['/status', '/presets', '/wifi-connect', '/wifi-scan']);
  assert.equal(ui.calls[3].options.method, 'POST');
  assert.equal(ui.el('manual-wifi').open, false);
  assert.equal(ui.intervals.length, 1);
  assert.equal(ui.timeouts.size, 0);
});

test('provider selection exposes custom settings and restores the verified Koofr preset', async () => {
  const ui = fixture(); await ui.ready();
  ui.el('provider').value = 'custom'; ui.el('provider').onchange();
  assert.equal(ui.el('server-url').readOnly, false);
  assert.equal(ui.el('ca-pem').readOnly, false);
  assert.equal(ui.el('server-details').open, true);
  assert.equal(ui.el('server-url').value, '');
  ui.el('provider').value = 'koofr'; ui.el('provider').onchange();
  assert.equal(ui.el('server-url').value, presetURL);
  assert.equal(ui.el('ca-pem').value, publicCA);
  assert.equal(ui.calls.length, 4);
});

test('discovered SSIDs stay text and only an explicit selection changes the Wi-Fi name', async () => {
  const hostileSSID = '\"><svg/onload=alert(1)>';
  const ui = fixture({scanStart: {status: 'ready', networks: [
    {ssid: hostileSSID, rssi: -45, secure: true},
    {ssid: 'Home Café', rssi: -52, secure: true},
    {ssid: 'Home Café', rssi: -70, secure: true},
    {ssid: '', rssi: -80, secure: false},
  ]}});
  await ui.ready(); ui.el('ssid').value = 'Manually entered network';
  await ui.el('scan').onclick();
  assert.equal(ui.el('ssid').value, 'Manually entered network');
  const options = ui.el('networks').children;
  assert.equal(options.length, 3);
  assert.equal(options[1].value, hostileSSID);
  assert.ok(options[1].textContent.startsWith(hostileSSID));
  assert.equal(options[1].children.length, 0);
  ui.el('networks').value = 'Home Café'; ui.el('networks').onchange();
  assert.equal(ui.el('ssid').value, 'Home Café');
  assert.equal(vm.runInContext('document.activeElement.id', ui.context), 'wifi-password');
});

test('scan start carries CSRF and empty JSON, then polls without a second mutation', async () => {
  const ui = fixture({scanStart: {state: 'scanning'}, scanPoll: {state: 'ready', networks: [{ssid: 'Home', rssi: -40, secure: true}]}});
  await ui.ready();
  assert.equal(ui.el('scan').disabled, true);
  const start = ui.calls.find(call => call.url === '/wifi-scan');
  assert.equal(start.options.method, 'POST');
  assert.equal(start.options.body, '{}');
  assert.equal(start.options.headers['Content-Type'], 'application/json');
  assert.equal(start.options.headers['X-CSRF-Token'], 'setup-csrf-token');
  await ui.timer();
  const scans = ui.calls.filter(call => call.url === '/wifi-scan');
  assert.equal(scans.length, 2);
  assert.equal(scans[1].options, undefined);
  assert.equal(ui.el('scan').disabled, false);
  assert.equal(ui.timeouts.size, 0);
});

test('SDK and HTTP scan failures preserve manually entered network credentials', async () => {
  for (const scenario of [{scanStart: {status: 'error'}}, {scanCode: 503}]) {
    const ui = fixture(scenario); await ui.ready();
    ui.el('ssid').value = 'My manual SSID'; ui.el('wifi-password').value = 'SyntheticWifiSecret';
    await ui.el('scan').onclick();
    assert.equal(ui.el('ssid').value, 'My manual SSID');
    assert.equal(ui.el('wifi-password').value, 'SyntheticWifiSecret');
    assert.equal(ui.el('scan').disabled, false);
    assert.equal(ui.timeouts.size, 0);
    assert.ok(ui.el('scan-note').textContent.length > 0);
  }
});

test('form submission sends typed settings and credentials only in a local JSON body', async () => {
  const ui = fixture(); await ui.ready();
  ui.el('ssid').value = 'Home test network';
  ui.el('wifi-password').value = 'SyntheticWifiSecret';
  ui.el('username').value = 'test-user@example.invalid';
  ui.el('cloud-password').value = 'SyntheticKoofrAppSecret';
  ui.el('interval').value = '120'; ui.el('max-size').value = '128';
  await ui.submit();
  const request = ui.calls.find(call => call.url === '/config');
  assert.ok(request); assert.equal(request.options.method, 'POST');
  assert.equal(request.options.headers['X-CSRF-Token'], 'setup-csrf-token');
  const body = JSON.parse(request.options.body);
  assert.equal(body.ssid, 'Home test network');
  assert.equal(body.wifi_password, 'SyntheticWifiSecret');
  assert.equal(body.wifi_open, false);
  assert.equal(body.username, 'test-user@example.invalid');
  assert.equal(body.password, 'SyntheticKoofrAppSecret');
  assert.equal(body.server_url, presetURL); assert.equal(body.ca_pem, publicCA);
  assert.equal(body.sync_interval_s, 120); assert.equal(body.max_file_size_mb, 128);
  assert.equal(typeof body.sync_interval_s, 'number');
  assert.equal(Object.hasOwn(body, 'provider'), false);
  for (const call of ui.calls) {
    for (const secret of [body.wifi_password, body.password, body.username]) {
      assert.equal(call.url.includes(secret), false);
      assert.equal(JSON.stringify(call.options?.headers || {}).includes(secret), false);
    }
  }
  assert.equal(ui.el('result').className, 'success');
});

test('missing setup authorization, presets or connection status prevents mutations', async () => {
  for (const scenario of [{statusCode: 503}, {presetsCode: 503}, {connectionCode: 503}]) {
    const ui = fixture(scenario); await ui.ready();
    assert.equal(ui.el('save').disabled, true);
    await ui.el('scan').onclick(); await ui.submit();
    assert.equal(ui.calls.some(call => call.options?.method === 'POST'), false);
    assert.equal(ui.el('save').disabled, true);
  }
});

test('selecting an open network clears its password and submits explicit open mode', async () => {
  const ui = fixture({scanStart: {state: 'ready', networks: [
    {ssid: 'Guest WiFi', rssi: -40, secure: false, personal: true},
    {ssid: 'Home WiFi', rssi: -50, secure: true, personal: true},
  ]}});
  await ui.ready();
  ui.el('wifi-password').value = 'PreviousSyntheticSecret';
  ui.el('networks').value = 'Guest WiFi'; ui.el('networks').onchange();
  assert.equal(ui.el('ssid').value, 'Guest WiFi');
  assert.equal(ui.el('open-wifi').checked, true);
  assert.equal(ui.el('wifi-password').value, '');
  assert.equal(ui.el('wifi-password').disabled, true);
  assert.equal(ui.el('wifi-password').required, false);
  await ui.submit();
  const body = JSON.parse(ui.calls.find(call => call.url === '/config').options.body);
  assert.equal(body.wifi_open, true); assert.equal(body.wifi_password, '');
  assert.equal(body.ssid, 'Guest WiFi');
  ui.el('networks').value = 'Home WiFi'; ui.el('networks').onchange();
  assert.equal(ui.el('open-wifi').checked, false);
  assert.equal(ui.el('wifi-password').disabled, false);
  assert.equal(ui.el('wifi-password').required, true);
  assert.equal(ui.el('wifi-password').value, '');
});

test('connecting checks the selected network and password before a local mutation', async () => {
  const ui = fixture(); await ui.ready();
  await ui.el('connect').onclick();
  assert.equal(ui.calls.some(call => call.url === '/wifi-connect' && call.options), false);
  assert.match(ui.el('connection-note').textContent, /Choose a Wi-Fi network/);
  ui.el('ssid').value = 'Home WiFi'; ui.el('wifi-password').value = 'short';
  await ui.el('connect').onclick();
  assert.equal(ui.calls.some(call => call.url === '/wifi-connect' && call.options), false);
  assert.match(ui.el('connection-note').textContent, /Wi-Fi password/);
  ui.el('open-wifi').checked = true; ui.el('open-wifi').onchange();
  await ui.el('connect').onclick();
  const request = ui.calls.find(call => call.url === '/wifi-connect' && call.options);
  assert.equal(request.options.headers['X-CSRF-Token'], 'setup-csrf-token');
  assert.equal(request.options.headers['Content-Type'], 'application/json');
  assert.deepEqual(JSON.parse(request.options.body), {ssid: 'Home WiFi', wifi_open: true, wifi_password: ''});
  assert.equal(ui.el('scan').disabled, true);
});

test('active guest connection shows only a static sign-in link and suppresses scan starts', async () => {
  for (const state of ['connecting', 'connected']) {
    const ui = fixture({connection: {state, message: 'Guest connection state'}}); await ui.ready();
    assert.equal(ui.calls.some(call => call.url === '/wifi-scan'), false);
    assert.equal(ui.el('scan').disabled, true);
    assert.equal(ui.el('guest-signin').hidden, state !== 'connected');
    assert.equal(ui.el('guest-signin').href, 'http://neverssl.com/');
    assert.equal(ui.el('guest-help').hidden, state !== 'connected');
    await ui.el('scan').onclick();
    assert.equal(ui.calls.some(call => call.url === '/wifi-scan'), false);
  }
});

test('disconnecting from guest Wi-Fi allows a fresh automatic scan', async () => {
  const ui = fixture({connection: {state: 'connected', message: 'Guest Wi-Fi connected'}}); await ui.ready();
  await ui.el('disconnect').onclick();
  const disconnect = ui.calls.find(call => call.url === '/wifi-disconnect');
  assert.equal(disconnect.options.method, 'POST'); assert.equal(disconnect.options.body, '{}');
  assert.equal(disconnect.options.headers['X-CSRF-Token'], 'setup-csrf-token');
  const scan = ui.calls.find(call => call.url === '/wifi-scan');
  assert.equal(scan.options.method, 'POST');
  assert.equal(ui.el('guest-signin').hidden, true);
  assert.equal(ui.el('disconnect').hidden, true);
  assert.equal(ui.el('scan').disabled, false);
});

test('subnet migration explains reconnecting and links only to a known local setup address', async () => {
  for(const setup_url of ['http://192.168.4.1/','http://172.31.254.1/','http://10.253.254.1/']){
    const ui=fixture({connection:{state:'address-change',message:'Setup address is changing',setup_url}});await ui.ready();
    assert.equal(ui.el('address-help').hidden,false);
    assert.equal(ui.el('setup-address').hidden,false);
    assert.equal(ui.el('setup-address').href,setup_url);
    assert.equal(ui.el('guest-signin').hidden,true);
    assert.equal(ui.el('scan').disabled,true);
    assert.equal(ui.el('connect').disabled,true);
    await ui.el('scan').onclick();
    assert.equal(ui.calls.some(call=>call.url==='/wifi-scan'),false);
  }
  for(const setup_url of ['javascript:alert(1)','https://attacker.example/','http://172.31.254.1.attacker.example/','http://172.31.254.1/?secret=x']){
    const ui=fixture({connection:{state:'address-change',setup_url}});await ui.ready();
    assert.equal(ui.el('setup-address').hidden,true);
    assert.equal(ui.el('setup-address').href,'#');
  }
});

test('wizard keeps one step visible and requires Wi-Fi and cloud details before review',async()=>{
  const ui=fixture({connection:{state:'connected',ssid:'Maimo Guestnet',wifi_open:true}});await ui.ready();
  assert.equal(ui.el('step-wifi').hidden,false);assert.equal(ui.el('step-cloud').hidden,true);assert.equal(ui.el('step-finish').hidden,true);
  assert.equal(ui.el('ssid').value,'Maimo Guestnet');assert.equal(ui.el('wifi-password').required,false);
  ui.el('next').onclick();
  assert.equal(ui.el('step-wifi').hidden,true);assert.equal(ui.el('step-cloud').hidden,false);
  ui.el('next').onclick();assert.equal(ui.el('step-finish').hidden,true);
  ui.el('username').value='test@example.invalid';ui.el('cloud-password').value='SyntheticAppPassword';
  ui.el('next').onclick();assert.equal(ui.el('step-finish').hidden,false);assert.equal(ui.el('next').hidden,true);
  assert.match(ui.el('review').textContent,/Maimo Guestnet/);assert.match(ui.el('review').textContent,/test@example.invalid/);
  assert.equal(ui.el('review').textContent.includes('SyntheticAppPassword'),false);
  ui.el('back').onclick();assert.equal(ui.el('step-cloud').hidden,false);assert.equal(ui.el('cloud-password').value,'SyntheticAppPassword');
});

test('reconnecting to a moved setup address reuses secured Wi-Fi only on the dongle',async()=>{
  const ui=fixture({connection:{state:'connected',ssid:'Private test WiFi',wifi_open:false}});await ui.ready();
  assert.equal(ui.el('wifi-password').value,'');assert.equal(ui.el('wifi-password').disabled,true);assert.equal(ui.el('wifi-password').required,false);
  ui.el('username').value='test@example.invalid';ui.el('cloud-password').value='SyntheticAppPassword';
  await ui.submit();
  const sent=JSON.parse(ui.calls.find(call=>call.url==='/config').options.body);
  assert.equal(sent.ssid,'Private test WiFi');assert.equal(sent.wifi_password,'');assert.equal(sent.reuse_wifi,true);
  assert.equal(ui.el('save').disabled,true);
  await ui.intervals[0].callback();assert.equal(ui.el('save').disabled,true);
});


test('configured devices open the dashboard without exposing saved credentials', async () => {
  const ui = fixture({status:{configured:true,wifi_connected:true,cloud_check:'ready',cloud_help:'Folder access passed',maintenance:true}, connection:{state:'connected',ssid:'Test WiFi',wifi_open:false}});
  await ui.ready();
  assert.equal(ui.el('home').hidden,false);assert.equal(ui.el('config').hidden,true);
  assert.equal(ui.el('setup-progress').hidden,true);
  assert.equal(ui.el('cloud-password').value,'');assert.equal(ui.el('username').value,'');
  assert.equal(ui.el('check-cloud').disabled,false);
  assert.match(ui.el('cloud-result').textContent,/Folder access passed/);
  assert.match(ui.el('backup-next').textContent,/Resume backups/);
  assert.equal(ui.calls.some(c=>c.url==='/cloud-test'),false);
});

test('cloud checks and synthetic uploads require explicit authenticated POST actions', async () => {
  const ui=fixture({status:{configured:true,wifi_connected:true,cloud_check:'ready'}});await ui.ready();
  await ui.el('check-cloud').onclick();await ui.el('test-cloud').onclick();
  for(const path of ['/cloud-check','/cloud-test']){
    const c=ui.calls.find(c=>c.url===path);assert.equal(c.options.method,'POST');
    assert.equal(c.options.body,'{}');assert.equal(c.options.headers['X-CSRF-Token'],'setup-csrf-token');
  }
});

test('editing Wi-Fi can retain the saved cloud account without returning or resending secrets',async()=>{
  const ui=fixture({status:{configured:true,wifi_connected:true,cloud_check:'ready'},connection:{state:'connected',ssid:'Test WiFi',wifi_open:false}});await ui.ready();
  ui.el('edit-settings').onclick();assert.equal(ui.el('home').hidden,true);assert.equal(ui.el('config').hidden,false);
  ui.el('next').onclick();ui.el('next').onclick();assert.equal(ui.el('step-finish').hidden,false);
  await ui.submit();const body=JSON.parse(ui.calls.find(c=>c.url==='/config').options.body);
  assert.equal(body.reuse_cloud,true);assert.equal(body.reuse_wifi,true);
  assert.equal(Object.hasOwn(body,'password'),false);assert.equal(Object.hasOwn(body,'username'),false);
});

test('changing the saved cloud account requires fresh credentials and one eject instructions',async()=>{
  const ui=fixture({status:{configured:true},connection:{state:'connected',ssid:'Test WiFi',wifi_open:true}});await ui.ready();
  ui.el('edit-settings').onclick();ui.el('next').onclick();ui.el('reuse-cloud').checked=false;ui.el('reuse-cloud').onchange();
  assert.equal(ui.el('cloud-fields').hidden,false);assert.equal(ui.el('cloud-password').required,true);
  ui.el('next').onclick();assert.equal(ui.el('step-finish').hidden,true);
  assert.ok(page.includes('This one-time eject applies the new settings.'));
  assert.ok(page.includes('backups run automatically when copying stops'));
  assert.ok(page.includes('href="/diagnostics" download="cloud-stick-diagnostics.json"'));
});


test('saved overnight settings survive editing Wi-Fi and are submitted as numbers',async()=>{
  const ui=fixture({status:{configured:true},connection:{state:'connected',ssid:'Test WiFi',wifi_open:true},saved:{backup_mode:'overnight',idle_seconds:180,window_start:22,window_end:5,timezone:'UTC0'}});await ui.ready();
  assert.equal(ui.el('backup-mode').value,'overnight');assert.equal(ui.el('overnight-options').hidden,false);
  await ui.submit();const d=JSON.parse(ui.calls.find(c=>c.url==='/config').options.body);
  assert.equal(d.idle_seconds,180);assert.equal(d.window_start,22);assert.equal(d.window_end,5);assert.equal(d.timezone,'UTC0');assert.equal(d.backup_mode,'overnight');
  ui.el('backup-mode').value='idle';ui.el('backup-mode').onchange();assert.equal(ui.el('overnight-options').hidden,true);
});
test('default background mode does not tell users to eject for normal backups',async()=>{
  const ui=fixture({status:{configured:true,maintenance:true}});await ui.ready();
  assert.equal(ui.el('backup-mode').value,'idle');assert.equal(ui.el('idle-seconds').value,'120');
  assert.match(ui.el('backup-next').textContent,/leave the USB plugged in/);assert.doesNotMatch(ui.el('backup-next').textContent,/eject/i);
  await ui.el('start-backup').onclick();assert.equal(ui.calls.find(c=>c.url==='/resume').options.body,'{}');
});

test('live dashboard avoids setup calls and safely renders persisted filenames', async()=>{
 const f=fixture({status:{configured:true,dashboard_only:true,wifi_connected:true,backup_active:true,transfer_phase:2,progress_percent:42,transfer_done_bytes:1048576,transfer_total_bytes:2097152,transfer_bytes_per_second:32768,watch_folder:'QA',verified:2,pending:1,failed:0},history:{files:[{path:'QA/<script>sample.pdf',state:'verified',bytes:2097152,verified_unix:1793680000}],more:false,cursor:''}});
 await f.ready();assert.equal(f.el('home').hidden,false);assert.equal(f.el('backup-headline').textContent,'Uploading to Koofr');assert.equal(f.el('transfer-progress').value,42);
 assert.equal(f.el('start-backup').hidden,true);assert.equal(f.el('edit-settings').hidden,true);assert.equal(f.el('file-history').children[0].children[0].textContent,'QA/<script>sample.pdf');
 assert.ok(f.calls.every(c=>['/status','/history'].includes(c.url)));assert.equal(f.calls.find(c=>c.url==='/history').options.headers['X-CSRF-Token'],'setup-csrf-token');
});

test('first history page refreshes automatically without interrupting paged browsing',async()=>{
 const f=fixture({status:{configured:true,dashboard_only:true},history:{files:[],more:true,cursor:'a'.repeat(52)}});await f.ready();
 const count=()=>f.calls.filter(c=>c.url==='/history').length;
 assert.equal(count(),1);f.advance(30001);await f.intervals[0].callback();await f.ready();assert.equal(count(),2);
 await f.el('more-history').onclick();await f.ready();assert.equal(count(),3);f.advance(30001);await f.intervals[0].callback();await f.ready();assert.equal(count(),3);
});

test('skipped files are visible and do not appear as an ordinary retry',async()=>{
 const f=fixture({status:{configured:true,dashboard_only:true,wifi_connected:true,has_scan_since_boot:true,skipped:1,error_code:-2107,error_help:'File exceeds your size limit.'}});await f.ready();
 assert.equal(f.el('count-skipped').textContent,'1');assert.equal(f.el('backup-headline').textContent,'Some files need attention');assert.equal(f.el('backup-description').textContent,'File exceeds your size limit.');
});

test('live filename and phase override an older pending receipt without HTML rendering',async()=>{
 const path='QA/<img src=x onerror=alert(1)>.pdf';
 const status={configured:true,dashboard_only:true,wifi_connected:true,backup_active:true,transfer_phase:2,current_file:path};
 const f=fixture({status,history:{files:[{path,state:'pending',bytes:2097152}],more:false,cursor:''}});await f.ready();
 assert.equal(f.el('backup-description').textContent,path);
 assert.equal(f.el('file-history').children[0].children[1].textContent,'Uploading now');
 status.transfer_phase=3;await f.intervals[0].callback();await f.ready();
 assert.equal(f.el('file-history').children[0].children[1].textContent,'Verifying now');
 status.backup_active=false;status.current_file='';await f.intervals[0].callback();await f.ready();
 assert.equal(f.el('file-history').children[0].children[1].textContent,'Waiting to upload');
});
