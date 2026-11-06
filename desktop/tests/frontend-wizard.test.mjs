import test from 'node:test';
import assert from 'node:assert/strict';
import {SetupWizardModel,setupSteps} from '../frontend/wizard-model.mjs';
const authenticated={authenticated:true,authenticatedGeneration:1,loginSupported:true};
function ready(){const m=new SetupWizardModel();m.next();m.checkedUsb({ownerReady:true});m.next();m.updateSession(authenticated);m.next();return m;}

test('welcome then USB; no device operation or save is produced by navigation',()=>{
 const m=new SetupWizardModel();assert.equal(m.step,'welcome');assert.equal(m.next(),true);
 assert.equal(m.step,'usb');assert.equal(m.next(),false);assert.deepEqual(setupSteps,['welcome','usb','auth','login','wifi','cloud','finish']);
 assert.equal(m.state.authenticated,false);
});
test('a blank or unsupported owner record cannot be bypassed by Next',()=>{
 const m=new SetupWizardModel();m.next();m.checkedUsb({ownerReady:false});assert.equal(m.next(),false);
 m.checkedUsb({ownerReady:'true'});assert.equal(m.next(),false);m.checkedUsb({ownerReady:true});assert.equal(m.next(),true);
 assert.equal(m.step,'auth');assert.equal(m.next(),false);
});
test('new account step requires explicit keep or a verified replacement reconnect',()=>{
 const m=ready();assert.equal(m.step,'login');assert.equal(m.next(),false);
 assert.equal(m.keepLogin(),true);assert.equal(m.next(),true);assert.equal(m.step,'wifi');
});
test('saving a new login returns to sign in and must authenticate a newer session',()=>{
 const m=ready();m.updateSession({...authenticated,authenticated:false,loginSaved:true});
 assert.equal(m.step,'auth');assert.equal(m.notice,'new-login');assert.equal(m.next(),false);
 m.updateSession(authenticated);assert.equal(m.loginReconnected,false);
 m.updateSession({...authenticated,authenticated:false});
 m.updateSession({...authenticated,authenticatedGeneration:2});assert.equal(m.loginReconnected,true);
 m.next();assert.equal(m.step,'wifi');
});
test('disconnect, timeout and page restoration remove navigation acceptance',()=>{
 for(const step of ['login','wifi','cloud','finish','records']){
  const m=ready();m.step=step;m.updateSession({authenticated:false});
  assert.equal(m.step,'auth');assert.equal(m.notice,'disconnected');assert.equal(m.finished,false);assert.equal(m.next(),false);
 }
});
test('unknown save outcomes never become completion or automatically retry',()=>{
 const m=ready();m.step='finish';m.updateSession({...authenticated,outcomeUnknown:true});
 assert.equal(m.step,'auth');assert.equal(m.notice,'unknown-save');assert.equal(m.next(),false);
 assert.equal(m.startFinish(),false);
});
test('valid saved Wi-Fi allows Next, but unchecked and unsaved candidates block it',()=>{
 const m=ready();m.keepLogin();m.next();assert.equal(m.next(),false);
 m.updateSession({...authenticated,wifiConfigured:true,wifiCandidatePending:true});assert.equal(m.next(),false);
 m.updateSession({...authenticated,wifiConfigured:true,wifiSavedThisSession:true});assert.equal(m.next(),true);assert.equal(m.step,'cloud');
});
test('pending operations and staged records block Back, Next and skip',()=>{
 for(const key of ['busy','loginStaged','cloudStaged','portalPrepared']){
  const m=ready();m.step='cloud';m.updateSession({...authenticated,[key]:true,cloudConfigured:true});
  assert.equal(m.next(),false);assert.equal(m.back(),false);assert.equal(m.keepLogin(),false);assert.equal(m.skipCloud(),false);
 }
});
test('unconfigured cloud requires explicit later choice; no verified-backup claim is inferred',()=>{
 const m=ready();m.step='cloud';assert.equal(m.next(),false);assert.equal(m.skipCloud(),true);
 assert.equal(m.next(),true);assert.equal(m.step,'finish');assert.equal(m.finished,false);
});
test('Finish is explicit disconnect and becomes finished only when link actually clears',()=>{
 const m=ready();m.step='finish';assert.equal(m.startFinish(),true);assert.equal(m.finished,false);
 m.updateSession({authenticated:false});assert.equal(m.finished,true);assert.equal(m.step,'finish');
});
test('changing USB clears checked-device and prior login choice',()=>{
 const m=ready();m.keepLogin();m.resetUsb();assert.equal(m.step,'usb');assert.equal(m.keptLogin,false);
 assert.equal(m.next(),false);
});
test('recorded status after disconnect requires a new authenticated connection',()=>{
 const m=new SetupWizardModel();m.records();assert.equal(m.step,'auth');assert.equal(m.next(),false);
 m.updateSession(authenticated);m.next();assert.equal(m.step,'records');
});
test('snapshot projection ignores secrets, malformed flags and invalid generations',()=>{
 const m=new SetupWizardModel();m.updateSession({authenticated:'true',password:'synthetic',owner_key:'synthetic',authenticatedGeneration:-1});
 assert.equal(m.state.authenticated,false);assert.equal(m.state.authenticatedGeneration,0);
 assert.equal('password' in m.state,false);assert.equal('owner_key' in m.state,false);
});
test('Back after finishing reconnects rather than showing empty protected settings',()=>{
 const m=ready();m.step='finish';m.startFinish();m.updateSession({authenticated:false});
 m.back();assert.equal(m.step,'auth');assert.equal(m.next(),false);
});
