import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {usbNextStep,usbFailureHelp} from '../frontend/onboarding.mjs';

test('USB discovery requires explicit choice before opening anything',()=>{
 assert.match(usbNextStep('usb_ports',[{port:'synthetic',label:'Synthetic USB'}]),/Choose.*Check this USB/);
 assert.match(usbNextStep('usb_ports',[]),/No supported USB.*plugged in/);
});
test('successful setup assistance explains short window and exact next click',()=>{
 const text=usbNextStep('usb_open_setup',{accepted:true});
 assert.match(text,/two minutes/);assert.match(text,/Find Cloud Stick/);assert.match(text,/CSP-W2-/);assert.match(text,/Leave the USB plugged in/);
 assert.doesNotMatch(text,/authenticated|saved|verified/);
});
test('unsuccessful setup does not claim connection and supplies physical fallback',()=>{
 const text=usbNextStep('usb_open_setup',{accepted:false});assert.match(text,/did not open/);assert.match(text,/BOOT.*three seconds/);
});
test('USB status does not confuse saved Wi-Fi with a verified backup',()=>{
 assert.match(usbNextStep('usb_status',{owner_ready:true,wifi:'local_connected'}),/does not prove a verified backup/);
 assert.match(usbNextStep('usb_status',{owner_ready:false}),/separate guided step/);
});
test('landing has sign-in instructions, no factory-login claim and read-only advanced update action',async()=>{
 const html=await readFile(new URL('../frontend/index.html',import.meta.url),'utf8'),bootstrap=await readFile(new URL('../frontend/bootstrap.mjs',import.meta.url),'utf8');
 assert.ok(html.indexOf('<header')<html.indexOf('id="desktop-toolbar"'));
 assert.match(html,/Enter the login saved on this stick/);assert.match(html,/This computer opens Bluetooth and finds the USB you selected/);assert.match(html,/key stays valid until you explicitly save/);
 assert.match(bootstrap,/summary\.textContent='Advanced: firmware bundle review'/);assert.match(bootstrap,/does not flash firmware or install app updates/);
 assert.match(bootstrap,/on-screen Disconnect button/);assert.doesNotMatch(bootstrap,/localStorage|sessionStorage|navigator\.bluetooth\s*=/);
});
test('Linux USB permissions provide access-support next step without implying a bad password',()=>{
 const text=usbFailureHelp({code:'usb_permission_denied',message:'SYNTHETIC-SECRET'});
 assert.match(text,/Linux device-access support/);assert.match(text,/computer permission issue, not an incorrect login password/);
 assert.doesNotMatch(text,/SYNTHETIC|sudo|chmod|dialout|\/dev\//);
});
test('busy and unknown USB ownership remain fail closed with explicit user retry',()=>{
 assert.match(usbFailureHelp({code:'usb_busy'}),/Close other setup apps or serial monitors/);
 const uncertain=usbFailureHelp({code:'usb_busy_unknown'});assert.match(uncertain,/cannot confirm/);assert.match(uncertain,/will not take over/);
});
test('timeouts and changed-device guidance do not automatically reconnect or advise interrupting work',()=>{
 assert.match(usbFailureHelp({code:'usb_timeout'}),/Wait for any current operation to finish/);
 assert.match(usbFailureHelp({code:'timeout'}),/will not start an automatic retry/);
 assert.match(usbFailureHelp({code:'usb_device_changed'}),/Find my USB/);
 for(const code of ['usb_timeout','timeout','usb_device_changed','usb_write_failed'])assert.doesNotMatch(usbFailureHelp({code}),/unplug|pull out|reset|erase|format/);
});
test('unknown and malformed failures never echo exception text or arbitrary codes',()=>{
 const fallback=usbFailureHelp({code:'unknown',message:'SYNTHETIC-PRIVATE-PATH'});
 for(const value of [{code:'SYNTHETIC-PRIVATE-CODE'},{code:'__proto__'},{code:15},new Error('SYNTHETIC-PRIVATE-PATH'),null])assert.equal(usbFailureHelp(value),fallback);
 assert.doesNotMatch(fallback,/SYNTHETIC|PRIVATE/);
});
