import {test} from 'node:test';
import assert from 'node:assert/strict';
import {WifiSetupClient,WifiInputError} from '../../phone/wifi-setup/connection.mjs';
function fixture(pages){
 let disconnected=false,saves=0;
 const row=i=>({ssid:'PUBLIC_SCAN_FIXTURE_'+i,rssi:-50,open:false,supported:true});
 const client=new WifiSetupClient({disconnect(){disconnected=true;},async command(op,args){
  if(op==='scan')return {ok:true};
  if(op==='scan_page'){const p=pages[args.cursor??0];assert(p);return {ok:true,networks:[row(args.cursor??0)],next:0,...p};}
  if(op==='commit')saves++;throw new Error('unexpected operation');
 }});
 return {client,get disconnected(){return disconnected;},get saves(){return saves;}};
}
test('all pages of a completed connected scan accept LocalOnly20 without any save',async()=>{
 const f=fixture({0:{wifi:'local_connected',has_scan:true,error:20,next:1},1:{wifi:'local_connected',has_scan:true,error:20}});
 assert.equal((await f.client.scan()).length,2);assert(!f.disconnected);assert.equal(f.saves,0);
});
for(const wifi of ['idle','local_connected'])test(wifi+' completed scan with error0 is retained',async()=>{
 const f=fixture({0:{wifi,has_scan:true,error:0}});assert.equal((await f.client.scan()).length,1);
});
for(const [name,p] of Object.entries({
 missing_results:{wifi:'local_connected',has_scan:false,error:20},
 failed_state:{wifi:'failed',has_scan:true,error:20},
 fault_state:{wifi:'fault',has_scan:true,error:20},
 idle_success_code_mismatch:{wifi:'idle',has_scan:true,error:20},
 genuine_error:{wifi:'local_connected',has_scan:true,error:15},
 omitted_status:{wifi:'local_connected',has_scan:true},
 string_status:{wifi:'local_connected',has_scan:true,error:'20'},
 unknown_state:{wifi:'unknown',has_scan:true,error:0}
}))test('rejects '+name+' without a save',async()=>{
 const f=fixture({0:p});await assert.rejects(f.client.scan(),e=>e instanceof WifiInputError&&e.code===15);assert.equal(f.saves,0);
});
test('a failing later page closes the protocol instead of mixing partial results',async()=>{
 const f=fixture({0:{wifi:'local_connected',has_scan:true,error:20,next:1},1:{wifi:'failed',has_scan:true,error:20}});
 await assert.rejects(f.client.scan(),/Network search changed/);assert(f.disconnected);assert.equal(f.saves,0);
});
