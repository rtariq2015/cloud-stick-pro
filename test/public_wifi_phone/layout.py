#!/usr/bin/env python3
"""Local-only Chromium rendering, with the Chromium sandbox required.

HTTPS responses are fulfilled from reviewed local files; no website, private
owner card, radio or cloud is contacted. Security2 is a transparent synthetic
test module, so this exercises UI layout/lifecycle, never real authentication.
"""
import asyncio
import hashlib
import json
import os
import resource
from pathlib import Path
from urllib.parse import urlsplit
from playwright.async_api import async_playwright

resource.setrlimit(resource.RLIMIT_CORE,(0,0))

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/public-wifi/phone/layout'
ORIGIN='https://synthetic-cloud-stick.invalid'
BROWSER='/config/.unmanic/research/cloud-stick-browsers/chromium_headless_shell-1248/chrome-headless-shell-linux64/chrome-headless-shell'
LIBRARIES='/config/.unmanic/research/cloud-stick-wifi-browser-deps/root/usr/lib/x86_64-linux-gnu:/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr/lib/x86_64-linux-gnu'
SECURITY='''export class ProtocolError extends Error{}
export class Security2Client {
 async start(){return Uint8Array.of(1)}
 async challenge(bytes){if(bytes[0]!==2)throw Error('fixture');return Uint8Array.of(3)}
 finish(bytes){if(bytes[0]!==4)throw Error('fixture')}
 async encrypt(bytes){return bytes.slice()}
 async decrypt(bytes){return bytes.slice()}
 destroy(){}
}'''
GATT=r'''(()=>{
 const encoder=new TextEncoder(),decoder=new TextDecoder();let step=0,job=0;
 const base='-0382-4aea-bff4-6b3f1c5adfb4';
 const version='021aff53'+base,session='021aff51'+base,info='021aff52'+base;
 const status={ok:true,revision:'6',durable:true,store_fault:false,wifi:'local_connected',wifi_error:20,wifi_profile:1,selected_profile:1,applied_revision:'6',internet_verified:false,cloud_available:false,backups_paused:true};
 const chars=new Map();
 const device={listeners:new Map(),addEventListener(n,f){this.listeners.set(n,f)},removeEventListener(n){this.listeners.delete(n)}};
 device.gatt={connected:false,async connect(){this.connected=true;return {async getPrimaryService(){return {async getCharacteristic(uuid){
  if(!chars.has(uuid)){let cached=new Uint8Array();chars.set(uuid,{
   async writeValueWithResponse(bytes){
    if(uuid===version){cached=encoder.encode(JSON.stringify({prov:{sec_ver:2,sec_patch_ver:1},csp:{milestone:2,synthetic:false,owner:'card',wifi:true,cloud:false,public_wifi:true,public_wifi_protocol:1,event_log:true,event_log_protocol:1}}));return}
    if(uuid===session){cached=Uint8Array.of(++step%2?2:4);return}
    const request=JSON.parse(decoder.decode(bytes));
    if(uuid===info){cached=encoder.encode(JSON.stringify({v:1,id:request.id,ok:true,model:'T-Dongle-S3',device_id:'CSP-W2-001122334455',firmware:'SYNTHETIC-LAYOUT',wifi_connected:true,backups_paused:true,synthetic:false}));return}
    let data;
    switch(request.op){
     case 'list':data={ok:true,revision:'6',count:1,profiles:[{id:1,ssid:'SYNTHETIC VENUE ONLY',open:true,has_secret:false,priority:0,enabled:true}],next:0};break;
     case 'status':data=status;break;
     case 'event_log':{const s=request.cursor+1;data={ok:true,events:s<=12?[{s,u:4294967295,t:253402300799,k:4,c:128}]:[],next:s<=12?s:request.cursor,more:s<12,newest:12,persisted:12,loaded:true,dirty:false,corrupt:false,io_failed:false,exhausted:false};break;}
     case 'portal_begin':data={ok:true,phase:'prepared',grant:'42',ap_ssid:'CSP-Guest-a1b2c3',ap_password:'admin123',profile:1,revision:'6',window_seconds:300,handoff_ms:30000,next:'handoff_then_disconnect'};break;
     case 'portal_handoff':data={ok:true,phase:'disconnect_to_continue',remaining_ms:9000,next:'read_result_then_disconnect',portal_error:0,attempt_dav_read_verified:false};break;
     default:throw Error('Unexpected layout operation '+request.op)
    }
    cached=encoder.encode(JSON.stringify({v:2,id:request.id,ok:true,job:++job,pending:false,data}));
   },async readValue(){const bytes=cached.slice();return new DataView(bytes.buffer)}
  })}return chars.get(uuid)
 }}}}},disconnect(){this.connected=false;step=0;chars.clear()}};
 Object.defineProperty(navigator,'bluetooth',{configurable:true,value:{async requestDevice(){return device}}});
})();'''

async def main():
 OUT.mkdir(parents=True,exist_ok=True)
 record={'status':'PENDING','sandbox_required':True,'widths':[320,390,800],
         'radio':'synthetic GATT only','security':'transparent synthetic fixture, not Security2 acceptance',
         'network':'all requests intercepted; no external requests permitted','screenshots':[]}
 record['source_sha256']={p:hashlib.sha256((ROOT/'phone/wifi-setup'/p).read_bytes()).hexdigest()
                          for p in ['index.html','style.css','app.mjs','connection.mjs','security2.mjs']}
 async with async_playwright() as p:
  try:
   browser=await p.chromium.launch(executable_path=BROWSER,headless=True,chromium_sandbox=True,
                                   env={**os.environ,'LD_LIBRARY_PATH':LIBRARIES})
  except Exception as error:
   (OUT/'sandbox-launch.log').write_text(str(error)+'\n')
   record.update(status='BLOCKED',reason='Sandboxed Chromium cannot launch in this container/host; see sandbox-launch.log. No sandbox bypass was used.')
   (OUT/'results.json').write_text(json.dumps(record,indent=2)+'\n')
   print(json.dumps(record,indent=2));return
  try:
   for width in record['widths']:
    context=await browser.new_context(viewport={'width':width,'height':900},device_scale_factor=1,service_workers='block')
    page=await context.new_page();errors=[];requests=[]
    page.on('pageerror',lambda error:errors.append(str(error)))
    async def route(request):
     target=urlsplit(request.request.url);requests.append(request.request.url)
     path=target.path.strip('/') or 'index.html'
     if target.scheme!='https' or target.netloc!='synthetic-cloud-stick.invalid' or path not in ['index.html','style.css','app.mjs','connection.mjs','security2.mjs']:
      raise AssertionError('Unexpected network request: '+request.request.url)
     body=SECURITY.encode() if path=='security2.mjs' else (ROOT/'phone/wifi-setup'/path).read_bytes()
     mime='text/css' if path.endswith('.css') else 'text/html' if path.endswith('.html') else 'text/javascript'
     await request.fulfill(status=200,body=body,headers={'Content-Type':mime+'; charset=utf-8','Permissions-Policy':'bluetooth=(self)','X-Content-Type-Options':'nosniff','Cache-Control':'no-store'})
    await page.route('**/*',route);await page.add_init_script(GATT);await page.goto(ORIGIN+'/index.html')
    await page.locator('#connect').wait_for(state='visible')
    async def capture(state):
     metrics=await page.evaluate('''()=>({width:innerWidth,scroll:document.documentElement.scrollWidth,controls:[...document.querySelectorAll('button,input')].filter(e=>e.getClientRects().length).map(e=>({id:e.id,left:e.getBoundingClientRect().left,right:e.getBoundingClientRect().right,width:e.getBoundingClientRect().width,height:e.getBoundingClientRect().height}))})''')
     assert metrics['scroll']<=width,f'{width}/{state}: horizontal document overflow'
     for control in metrics['controls']:
      assert control['left']>=0 and control['right']<=width+1,f'{width}/{state}: clipped {control}'
      if control['id'] not in ['manual-open','show-owner-key','show-wifi-password','accept-local','keep-secret']:
       assert control['height']>=44,f'{width}/{state}: small touch target {control}'
     name=f'{width}-{state}.png';await page.screenshot(path=str(OUT/name),full_page=True)
     record['screenshots'].append({'file':name,'sha256':hashlib.sha256((OUT/name).read_bytes()).hexdigest(),'metrics':metrics})
    await capture('disconnected')
    await page.locator('#owner-key').fill('PUBLIC_SYNTHETIC_OWNER_KEY_ONLY_32');await page.locator('#connect').click()
    await page.locator('#portal-card').wait_for(state='visible');assert await page.locator('#connection-state').inner_text()=='Private owner authenticated'
    await capture('authenticated')
    await page.locator('#portal-prepare').click();await page.locator('#portal-ready').wait_for(state='visible');await capture('prepared')
    await page.locator('#portal-continue').click();await page.locator('#portal-handoff-card').wait_for(state='visible');await capture('instructions')
    assert await page.locator('#connection-state').inner_text()!='Private owner authenticated'
    assert await page.locator('#owner-key').input_value()==''
    assert not await page.locator('#portal-card').is_visible()
    assert not errors,errors
    await context.close()
   record['status']='PASS'
  except Exception as error:
   record.update(status='FAIL',reason=repr(error));raise
  finally:
   await browser.close();(OUT/'results.json').write_text(json.dumps(record,indent=2)+'\n')
 print(json.dumps(record,indent=2))

if __name__=='__main__':asyncio.run(main())
