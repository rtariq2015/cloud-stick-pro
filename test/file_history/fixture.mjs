import {fixture as gattFixture} from '../public_wifi_phone/fixture.mjs';
export {until} from '../public_wifi_phone/fixture.mjs';
export {FileHistoryClient,WifiSetupClient,WifiInputError,PublicWifiClient,DiagnosticLogClient} from '../../phone/wifi-setup/connection.mjs';
export const rows=[
 {path:'QA/<img src=x onerror=alert(1)>.pdf',bytes:'2097152',state:2,verified:'1793793600',retry:'0',error:0,attempts:0},
 {path:'QA/'+('x'.repeat(60))+'📄'.repeat(44)+'z',bytes:'18446744073709551615',state:1,verified:'0',retry:'0',error:0,attempts:0},
 {path:'QA/retry.pdf',bytes:'8388608',state:0,verified:'0',retry:'1793793610',error:-3,attempts:2},
];
export function statusFor(list,change={}){return {ok:true,g:3,n:list.length,matched:list.length,when:'1793793600',loaded:true,complete:true,limited:false,corrupt:false,io_failed:false,allocation_failed:false,exhausted:false,...change};}
export function fragmentFor(list,request){
 const row=list[request.i],bytes=new TextEncoder().encode(row.path),next=Math.min(bytes.length,request.o+64);
 return {ok:true,g:3,i:request.i,n:list.length,b:row.bytes,s:row.state,v:row.verified,r:row.retry,e:row.error,a:row.attempts,p:bytes.length,o:request.o,x:next,m:next<bytes.length,h:Array.from(bytes.subarray(request.o,next),byte=>byte.toString(16).padStart(2,'0')).join('')};
}
export function fixture(extra={}){
 const list=extra.rows??rows;
 const options={...extra,status:extra.wifiStatus,metadata:meta=>{
  if(extra.capability===undefined){meta.csp.file_history=true;meta.csp.file_history_protocol=1;}
  else if(extra.capability!==null){meta.csp.file_history=extra.capability;if(extra.capability===true)meta.csp.file_history_protocol=1;}
  return extra.metadata?extra.metadata(meta):meta;
 },reply:(op,request)=>{
  if(op==='history_status'){if(extra.delayStatus)options.delayOp=op;return statusFor(list,extra.status);}
  if(op==='history_row'){
   if(extra.delayOffset===request.o)options.delayOp=op;
   const result=fragmentFor(list,request);return extra.fragment?extra.fragment(result,request):result;
  }
  return extra.reply?.(op,request);
 }};
 return gattFixture(options);
}
