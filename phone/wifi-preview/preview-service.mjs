// UI fixture only. This does not authenticate an owner or implement the C++
// transaction policy. A future adapter must use that policy behind Security 2.
// Intentionally no Bluetooth/network/storage imports, APIs, or real input.
const fixtures = Object.freeze([
  Object.freeze({id:1,ssid:'CSP Test Private',security:'WPA2 · Password required',open:false,selectable:true}),
  Object.freeze({id:2,ssid:'CSP Test Guest',security:'Open · May need an Accept page',open:true,selectable:true}),
  Object.freeze({id:3,ssid:'CSP Test Enterprise',security:'Enterprise · Not supported in this prototype',open:false,selectable:false}),
]);
const hidden = Object.freeze({id:4,ssid:'CSP Test Hidden',security:'WPA2 · Hidden example',open:false,selectable:true});
export const messages = Object.freeze({
  ready:'The example connection passed. You can keep these demo settings.',
  wrong_password:'The password was rejected. Your current network is unchanged. Check the password and try again.',
  captive:'This network needs an Accept page. It is not ready for backups. Your current settings are unchanged.',
  dhcp:'The network did not give the stick an address. Your current settings are unchanged. Try again later.',
  dns:'The network connected, but DNS is unavailable. Your current settings are unchanged.',
});
export class PreviewService {
  #generation=0; #candidate=null; #ready=false; #active='CSP Test Home'; #closed=false;
  constructor({delay=ms=>new Promise(resolve=>setTimeout(resolve,ms))}={}) {this.delay=delay;}
  get active() {return this.#active;}
  async scan() {
    if(this.#closed)throw new Error('Preview closed');
    const generation=this.#generation;await this.delay(100);
    if(this.#closed||generation!==this.#generation)return null;
    return fixtures;
  }
  select(id) {
    if(this.#closed)throw new Error('Preview closed');
    const row=[...fixtures,hidden].find(row=>row.id===id&&row.selectable);
    if(!row)throw new Error('Only selectable example networks are available');
    ++this.#generation;this.#candidate=row;this.#ready=false;
    return row;
  }
  invalidateCheck() {++this.#generation;this.#ready=false;}
  async test(outcome,onProgress=()=>{}) {
    if(this.#closed||!this.#candidate||!Object.hasOwn(messages,outcome))throw new Error('Invalid example check');
    const generation=++this.#generation;this.#ready=false;
    for(const stage of ['Checking Wi-Fi…','Checking network address…','Checking DNS and endpoint…']) {
      if(this.#closed||generation!==this.#generation)return null;
      onProgress(stage);await this.delay(100);
    }
    if(this.#closed||generation!==this.#generation)return null;
    this.#ready=outcome==='ready';return {ready:this.#ready,message:messages[outcome]};
  }
  save() {
    if(this.#closed||!this.#candidate||!this.#ready)throw new Error('Check the exact example candidate first');
    this.#active=this.#candidate.ssid;this.cancel();return this.#active;
  }
  cancel() {++this.#generation;this.#candidate=null;this.#ready=false;}
  close() {this.cancel();this.#closed=true;}
}
