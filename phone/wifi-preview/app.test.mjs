// Actual app body and actual fixture service; simulated DOM/events, no browser.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {PreviewService} from './preview-service.mjs';
function fixture({defer=false}={}) {
 const windowEvents=new Map(),documentEvents=new Map(),releases=[];
 class Element {
  hidden=false; disabled=false; textContent=''; value=''; className=''; children=[];
  append(child){this.children.push(child);}
  replaceChildren(...children){this.children=children;}
  focus(){}
  querySelector(){return this.option??=(new Element());}
 }
 const ids=['candidate','password','check-result','save','test','scan','scan-status','networks','selected-name','selected-security','password-row','open-note','outcome','manual','active-network','saved-state','cancel'];
 const elements=Object.fromEntries(ids.map(id=>[id,new Element()]));
 elements.candidate.hidden=elements.save.hidden=true;elements['active-network'].textContent='CSP Test Home';
 const document={visibilityState:'visible',getElementById:id=>elements[id],
  createElement:()=>new Element(),createTextNode:text=>({textContent:text}),
  addEventListener:(name,fn)=>documentEvents.set(name,fn)};
 class Service extends PreviewService {constructor(){super({delay:defer?()=>new Promise(resolve=>releases.push(resolve)):()=>Promise.resolve()});}}
 vm.runInNewContext(readFileSync(new URL('./app.mjs',import.meta.url),'utf8').replace(/^import[^\n]*\n/,''),
  {document,PreviewService:Service,addEventListener:(name,fn)=>windowEvents.set(name,fn)});
 return {elements,document,releases,event:name=>(windowEvents.get(name)||documentEvents.get(name))?.({persisted:true})};
}
test('scan creates selectable buttons; explicit test then save; secrets cleared',async()=>{
 const {elements:e}=fixture();await e.scan.onclick();assert.equal(e.networks.children.length,3);
 assert(e.networks.children[2].children[0].disabled);
 e.networks.children[0].children[0].onclick();assert.equal(e.password.value,'testpass123');
 assert.equal(e['active-network'].textContent,'CSP Test Home');assert(e.save.hidden);
 e.outcome.value='wrong_password';await e.test.onclick();assert(e.save.hidden);
 assert.match(e['check-result'].textContent,/password was rejected/);
 e.outcome.value='ready';e.outcome.onchange();await e.test.onclick();assert(!e.save.hidden);
 e.save.onclick();assert.equal(e['active-network'].textContent,'CSP Test Private');
 assert.equal(e.password.value,'');assert(e.candidate.hidden);assert(e.save.hidden);
});
test('open and hidden flows distinguish password and portal behavior',async()=>{
 const {elements:e}=fixture();await e.scan.onclick();e.networks.children[1].children[0].onclick();
 assert(e['password-row'].hidden);assert(!e['open-note'].hidden);assert.equal(e.password.value,'');
 assert(e.outcome.querySelector().disabled);
 e.outcome.value='captive';await e.test.onclick();assert(e.save.hidden);
 assert.match(e['check-result'].textContent,/Accept page/);
 e.manual.onclick();assert.equal(e['selected-name'].textContent,'CSP Test Hidden');
 assert(!e['password-row'].hidden);assert(!e.outcome.querySelector().disabled);
});
for(const reason of ['pagehide','pageshow','freeze','visibilitychange'])test(reason+' clears draft and rejects late result',async()=>{
 const f=fixture({defer:true}),e=f.elements;e.manual.onclick();
 const pending=e.test.onclick();assert.equal(f.releases.length,1);
 f.document.visibilityState='hidden';f.event(reason);f.releases.shift()();await pending;
 assert(e.candidate.hidden);assert(e.save.hidden);assert.equal(e.password.value,'');
 assert.equal(e['active-network'].textContent,'CSP Test Home');
});
test('changing outcome invalidates an earlier successful check',async()=>{
 const {elements:e}=fixture();e.manual.onclick();await e.test.onclick();assert(!e.save.hidden);
 e.outcome.value='captive';e.outcome.onchange();assert(e.save.hidden);
 assert.throws(()=>e.save.onclick());assert.equal(e['active-network'].textContent,'CSP Test Home');
});
test('late scan does not repopulate a suspended screen',async()=>{
 const f=fixture({defer:true}),e=f.elements,pending=e.scan.onclick();
 f.event('freeze');f.releases.shift()();await pending;
 assert.equal(e.networks.children.length,0);assert.equal(e.scan.disabled,false);
});
