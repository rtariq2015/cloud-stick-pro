import {fixture as gattFixture} from '../public_wifi_phone/fixture.mjs';
export {until} from '../public_wifi_phone/fixture.mjs';
export {DiagnosticLogClient,WifiSetupClient,WifiInputError,PublicWifiClient} from '../../phone/wifi-setup/connection.mjs';
export const events=[{s:21,u:10,t:0,k:2,c:0},{s:22,u:20,t:1793793600,k:6,c:-2303},{s:23,u:30,t:1793793600,k:4,c:2}];
export function pageFor(rows,cursor,change={}){
 const newer=rows.filter(event=>event.s>cursor),row=newer[0],newest=rows.at(-1)?.s??0;
 return {ok:true,events:row?[{...row}]:[],next:row?.s??cursor,more:newer.length>1,newest,persisted:newest,
  loaded:true,dirty:false,corrupt:false,io_failed:false,exhausted:false,...change};
}
export function fixture(extra={}){
 const rows=extra.events??events;
 const options={...extra,status:extra.wifiStatus,metadata:meta=>{
  if(extra.capability===undefined){meta.csp.event_log=true;meta.csp.event_log_protocol=1;}
  else if(extra.capability!==null){meta.csp.event_log=extra.capability;if(extra.capability===true)meta.csp.event_log_protocol=1;}
  return extra.metadata?extra.metadata(meta):meta;
 },reply:(op,request)=>{
  if(op==='event_log'){
   if(extra.delayCursor===request.cursor)options.delayOp='event_log';
   const result=pageFor(rows,request.cursor,extra.status);
   return extra.page?extra.page(result,request):result;
  }
  return extra.reply?.(op,request);
 }};
 return gattFixture(options);
}
