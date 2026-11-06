import {test} from 'node:test';
import assert from 'node:assert/strict';
import {PreviewService,messages} from './preview-service.mjs';
const instant=()=>Promise.resolve();
test('only explicit selectable fixtures, no arbitrary credentials or network names',async()=>{
 const service=new PreviewService({delay:instant}),rows=await service.scan();
 assert.equal(rows.length,3);assert(Object.isFrozen(rows));
 assert.throws(()=>service.select(3));assert.throws(()=>service.select(100));
 assert.equal(service.select(2).open,true);assert.equal(service.select(4).ssid,'CSP Test Hidden');
 assert.throws(()=>service.save());assert.equal(service.active,'CSP Test Home');
});
test('failure and portal outcomes preserve previous settings and block save',async()=>{
 for(const outcome of Object.keys(messages).filter(key=>key!=='ready')){
  const service=new PreviewService({delay:instant});service.select(1);
  const result=await service.test(outcome);assert.equal(result.ready,false);
  assert.throws(()=>service.save());assert.equal(service.active,'CSP Test Home');
 }
});
test('successful exact candidate is saved only in transient fixture state',async()=>{
 const service=new PreviewService({delay:instant});service.select(1);
 assert.equal((await service.test('ready')).ready,true);assert.equal(service.save(),'CSP Test Private');
 assert.throws(()=>service.save());assert.equal(new PreviewService().active,'CSP Test Home');
});
test('late check cannot pass after select, cancel, outcome change or close',async()=>{
 for(const action of ['select','cancel','invalidateCheck','close']){
  let release;const service=new PreviewService({delay:()=>new Promise(resolve=>{release=resolve;})});
  service.select(1);const pending=service.test('ready');
  if(action==='select')service.select(2);else service[action]();
  release();assert.equal(await pending,null);assert.throws(()=>service.save());
  assert.equal(service.active,'CSP Test Home');
 }
});
test('cancelled scan cannot repopulate a restored screen',async()=>{
 let release;const service=new PreviewService({delay:()=>new Promise(resolve=>{release=resolve;})});
 const scan=service.scan();service.cancel();release();assert.equal(await scan,null);
});
test('earlier concurrent check cannot override later result',async()=>{
 const releases=[];const service=new PreviewService({delay:()=>new Promise(resolve=>releases.push(resolve))});
 service.select(1);const first=service.test('ready');const second=service.test('wrong_password');
 releases.shift()();assert.equal(await first,null);
 for(let i=0;i<3;++i){releases.shift()();await Promise.resolve();}
 assert.equal((await second).ready,false);assert.throws(()=>service.save());
});
