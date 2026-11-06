// Only these non-identifying facts are published to the desktop wizard. They
// live in RAM and never contain protocol responses, names or credentials.
export const SETUP_BOOLEAN_FIELDS=Object.freeze(['authenticated','busy','loginSupported','loginStaged','cloudStaged','portalPrepared','wifiConfigured','wifiCandidatePending','wifiSavedThisSession','cloudConfigured','cloudSavedThisSession','loginSaved','wifiSaveUnknown','cloudSaveUnknown','outcomeUnknown']);
const liveFields=['loginSupported','loginStaged','cloudStaged','portalPrepared','wifiConfigured','wifiCandidatePending','wifiSavedThisSession','cloudConfigured','cloudSavedThisSession'];
export class SetupStatePublisher {
 #snapshot;#emit;
 constructor(emit=()=>{}){this.#emit=emit;this.#snapshot=Object.freeze({...Object.fromEntries(SETUP_BOOLEAN_FIELDS.map(k=>[k,false])),authenticatedGeneration:0});}
 get(){return this.#snapshot;}
 publish(value){
  if(!value||typeof value!=='object'||SETUP_BOOLEAN_FIELDS.some(k=>typeof value[k]!=='boolean')||!Number.isSafeInteger(value.authenticatedGeneration)||value.authenticatedGeneration<0)throw new TypeError('Invalid setup state');
  const next=Object.fromEntries(SETUP_BOOLEAN_FIELDS.map(k=>[k,value[k]]));next.authenticatedGeneration=value.authenticatedGeneration;
  if(next.authenticatedGeneration<this.#snapshot.authenticatedGeneration)throw new TypeError('Stale setup state');
  if(!next.authenticated)for(const key of liveFields)next[key]=false;
  next.outcomeUnknown=next.outcomeUnknown||next.wifiSaveUnknown||next.cloudSaveUnknown;
  const snapshot=Object.freeze(next),changed=Object.keys(snapshot).some(k=>snapshot[k]!==this.#snapshot[k]);
  this.#snapshot=snapshot;if(changed)this.#emit(snapshot);return snapshot;
 }
}
const publisher=new SetupStatePublisher(snapshot=>{
 if(typeof globalThis.dispatchEvent==='function'&&typeof globalThis.CustomEvent==='function')globalThis.dispatchEvent(new CustomEvent('cloudstick:setup-state',{detail:snapshot}));
});
export const publishSetupState=value=>publisher.publish(value);
// Filled by bootstrap/app; no navigator override or credential persistence.
export const desktopContext={bluetooth:null,secureContext:false,closeTransport:null,getSetupState:()=>publisher.get(),closeSetup:()=>{},prepareSetupAccess:()=>false,connectSetupAccess:async()=>false,connectFirstOwner:async()=>false,cancelSetupAccess:()=>{},setSetupAccessPending:()=>{},startDirectSetup:null,reviewFirstOwner:null,checkFirstOwner:null,installFirstOwner:null,confirmFirstOwner:null,cancelFirstOwner:()=>{}};
