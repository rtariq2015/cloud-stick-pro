// SPDX-License-Identifier: MIT
const privateIp=value=>typeof value==='string'&&/^(?:10\.|192\.168\.|172\.(?:1[6-9]|2[0-9]|3[01])\.)/.test(value)&&/^\d{1,3}(?:\.\d{1,3}){3}$/.test(value)&&value.split('.').every(x=>Number(x)<=255&&String(Number(x))===x);
export function mountLive({document,request,idle,setTimer=setTimeout,clearTimer=clearTimeout}){
 const $=id=>document.getElementById(id);let address='',pending=false,timer=null,stopped=false,epoch=0;
 async function read(){
  if(stopped||pending||!address||!idle())return;pending=true;const token=epoch;
  try{const s=await request('lan_status',{address});if(token!==epoch||stopped)return;
   if(!s||!['verified','pending','failed'].every(k=>Number.isSafeInteger(s[k])&&s[k]>=0)||typeof s.paused!=='boolean')throw Error();
   for(const k of ['verified','pending','failed'])$('live-'+k).textContent=String(s[k]);
   $('live-message').textContent=s.paused?'Backups are paused while setup closes.':s.active?'Your stick is working on its backups.':s.wifi?'Connected over Wi-Fi. Your stick is ready.':'Your stick is waiting for Wi-Fi.';
   $('live-address').textContent=address+' · Firmware '+s.firmware;$('live-summary').hidden=false;
  }catch{if(token===epoch){$('live-message').textContent='Live status is unavailable. Check Wi-Fi, or use Bluetooth setup below.';$('live-summary').hidden=true;}}
  finally{pending=false;}
 }
 async function discover(){
  if(pending||!idle()||stopped)return;pending=true;const token=epoch;$('live-message').textContent='Looking on your local Wi-Fi…';
  try{const rows=await request('lan_discover');if(token!==epoch||stopped)return;if(!Array.isArray(rows)||rows.length>16||!rows.every(privateIp))throw Error();
   const select=$('live-devices');select.replaceChildren();
   for(const ip of rows){const o=document.createElement('option');o.value=ip;o.textContent=ip;select.append(o);}
   select.hidden=rows.length<2;
   if(rows.length===1)setAddress(rows[0]);
   else $('live-message').textContent=rows.length?'Choose your stick’s address. Discovery does not sign you in.':'No announcing stick found. A plugged-in supported stick supplies its address automatically. Older firmware or Wi-Fi isolation may require manual entry.';
  }catch{$('live-message').textContent='Local discovery is unavailable. Plug the stick in or enter its address.';}finally{pending=false;}void read();
 }
 function setAddress(value){if(!privateIp(value))return;epoch++;address=value;$('live-manual-address').value=value;void read();}
 $('live-find').addEventListener('click',discover);$('live-devices').addEventListener('change',()=>setAddress($('live-devices').value));
 $('live-address-form').addEventListener('submit',e=>{e.preventDefault();const value=$('live-manual-address').value.trim();if(!privateIp(value)){$('live-message').textContent='Enter the private IP shown on your stick.';return;}setAddress(value);});
 function poll(){if(stopped)return;void read();timer=setTimer(poll,5000);}poll();
 return {setAddress,stop(){stopped=true;epoch++;clearTimer(timer);}};
}
