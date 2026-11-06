import {PreviewService} from './preview-service.mjs';
const $=id=>document.getElementById(id);
const service=new PreviewService();
let attempt=0,scanning=false;
function clearDraft() {
  ++attempt;service.cancel();$('candidate').hidden=true;$('password').value='';
  $('check-result').textContent='';$('save').hidden=true;$('test').disabled=false;
}
function choose(id) {
  clearDraft();const row=service.select(id);
  $('selected-name').textContent=row.ssid;$('selected-security').textContent=row.security;
  $('password-row').hidden=row.open;$('open-note').hidden=!row.open;
  $('password').value=row.open?'':'testpass123';$('outcome').value='ready';
  $('outcome').querySelector('[value="wrong_password"]').disabled=row.open;
  $('scan-status').textContent='Example selected. Check it before keeping the settings.';
  $('candidate').hidden=false;$('test').focus();
}
$('scan').onclick=async()=>{
  if(scanning)return;
  scanning=true;$('scan').disabled=true;$('scan-status').textContent='Searching example networks…';
  try {
    const rows=await service.scan();if(!rows)return;
    $('networks').replaceChildren();
    for(const row of rows) {
      const li=document.createElement('li'),button=document.createElement('button');
      button.type='button';button.className='network';button.disabled=!row.selectable;
      button.append(document.createTextNode(row.ssid));
      const detail=document.createElement('span');detail.textContent=row.security;button.append(detail);
      button.onclick=()=>choose(row.id);li.append(button);$('networks').append(li);
    }
    $('scan-status').textContent='Three example networks. Choose only a network you are allowed to use.';
  } finally {scanning=false;$('scan').disabled=false;}
};
$('manual').onclick=()=>choose(4);
$('outcome').onchange=()=>{++attempt;service.invalidateCheck();$('save').hidden=true;$('check-result').textContent='Run the check again for this result.';$('test').disabled=false;};
$('test').onclick=async()=>{
  const current=++attempt;$('test').disabled=true;$('save').hidden=true;$('check-result').className='result';
  try {
    const result=await service.test($('outcome').value,message=>{if(current===attempt)$('check-result').textContent=message;});
    if(!result||current!==attempt)return;
    $('check-result').textContent=result.message;$('check-result').className='result '+(result.ready?'success':'error');
    $('save').hidden=!result.ready;
  } finally {if(current===attempt)$('test').disabled=false;}
};
$('save').onclick=()=>{
  $('active-network').textContent=service.save();$('saved-state').textContent='Demo memory only';
  clearDraft();$('scan-status').textContent='Example saved in page memory. Reloading restores the starting example.';
};
$('cancel').onclick=()=>{clearDraft();$('scan-status').textContent='Changes cancelled. Your current example network is unchanged.';};
function suspend(){clearDraft();$('networks').replaceChildren();$('scan-status').textContent='Preview paused. Search again to continue.';}
addEventListener('pagehide',suspend);
addEventListener('pageshow',event=>{if(event.persisted)suspend();});
document.addEventListener('visibilitychange',()=>{if(document.visibilityState!=='visible')suspend();});
document.addEventListener('freeze',suspend);
