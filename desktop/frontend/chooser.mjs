// Discovery results are text, never HTML. A user must choose one exact device.
export function createDeviceChooser(document){
 const dialog=document.createElement('dialog');dialog.className='device-dialog';dialog.setAttribute('aria-labelledby','desktop-choose-title');
 const title=document.createElement('h2');title.id='desktop-choose-title';title.textContent='Choose your Cloud Stick';
 const hint=document.createElement('p');hint.textContent='Settings use this computer’s Bluetooth. Open the stick’s setup window, then choose its name.';
 const rows=document.createElement('div'),message=document.createElement('p');message.setAttribute('role','status');
 const cancel=document.createElement('button');cancel.type='button';cancel.className='text-button';cancel.textContent='Cancel';
 dialog.append(title,hint,message,rows,cancel);document.body.append(dialog);let settle=null,previous=null;
 function finish(value){if(!settle)return;const done=settle;settle=null;dialog.close();rows.replaceChildren();previous?.focus?.();done(value);}
 cancel.onclick=()=>finish(null);dialog.addEventListener('cancel',event=>{event.preventDefault();finish(null);});
 return {choose(devices){
  if(settle)return Promise.reject(new Error('A device chooser is already open.'));
  previous=document.activeElement;rows.replaceChildren();message.textContent=devices.length?'Choose the name shown by your stick.':'No Cloud Stick found. Cancel, open its setup window and try again.';
  for(const row of devices){const b=document.createElement('button');b.type='button';b.textContent=row.name||'Cloud Stick';b.onclick=()=>finish(row.id);rows.append(b);}
  return new Promise(resolve=>{settle=resolve;dialog.showModal();(rows.firstElementChild??cancel).focus();});
 },cancel(){finish(null);},destroy(){finish(null);dialog.remove();}};
}
