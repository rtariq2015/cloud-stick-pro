// SPDX-FileCopyrightText: 2018-2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// Browser port of ESP-IDF v5.5.5 tools/esp_prov/security/{security2,srp6a}.py.
// Changes: Web Crypto SHA-512/AES-GCM, bounded strict protobuf reader, mandatory
// device proof, patch-1 nonce only, fail-closed lifecycle. Standard SRP6a wire
// protocol; no new cipher or key exchange. See PROVENANCE.md and vendor license.
// This prototype accepts synthetic ownership credentials only. JavaScript
// BigInt exponentiation is not constant-time; production review is outstanding.

const encoder = new TextEncoder();
const N = BigInt('0x' +
  'FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B' +
  '139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485' +
  'B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1' +
  'FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D2' +
  '3DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C3' +
  '2905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955' +
  '817183995497CEA956AE515D2261898FA051015728E5A8AAAC42DAD33170D04507A33A855' +
  '21ABDF1CBA64ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7ABF5AE8CDB093' +
  '3D71E8C94E04A25619DCEE3D2261AD2EE6BF12FFA06D98A0864D87602733EC86A64521F2B' +
  '18177B200CBBE117577A615D6C770988C0BAD946E208E24FA074E5AB3143DB5BFCE0FD108E' +
  '4B82D120A93AD2CAFFFFFFFFFFFFFFFF');
const G = 5n;
export const MAX_PACKET = 1024;
export class ProtocolError extends Error {}
const fail = message => { throw new ProtocolError(message); };
export function bytes(value) {
  if (!(value instanceof Uint8Array)) fail('Expected byte array');
  if (value.length > MAX_PACKET) fail('Packet is too large');
  return value;
}
function join(...parts) {
  const out = new Uint8Array(parts.reduce((n,p) => n+p.length,0));
  let offset=0; for(const p of parts){out.set(p,offset);offset+=p.length;} return out;
}
function intBytes(value) {
  let hex=value.toString(16); if(hex.length%2)hex='0'+hex;
  return Uint8Array.from(hex.match(/../g).map(x=>parseInt(x,16)));
}
function integer(value) { return BigInt('0x'+Array.from(value,b=>b.toString(16).padStart(2,'0')).join('')); }
function padded(value) {const raw=intBytes(value);if(raw.length>384)fail('SRP integer too large');const p=new Uint8Array(384);p.set(raw,384-raw.length);return p;}
// Direct translation of Python's pow(base, exponent, N), for standard SRP only.
function modPow(base,exponent) {
  let out=1n;base=((base%N)+N)%N;
  while(exponent>0n){if(exponent&1n)out=out*base%N;exponent>>=1n;base=base*base%N;}return out;
}
function equal(a,b) { if(a.length!==b.length)return false;let diff=0;for(let i=0;i<a.length;i++)diff|=a[i]^b[i];return diff===0; }
function varint(n) {const b=[];do{let v=n%128;n=Math.floor(n/128);b.push(v|(n?128:0));}while(n);return Uint8Array.from(b);}
export function field(number,value) {
  return typeof value==='number'?join(varint(number*8),varint(value)):join(varint(number*8+2),varint(value.length),value);
}
export function fields(packet,allowed) {
  bytes(packet);let offset=0;const result=new Map();
  const read=()=>{let n=0,factor=1;for(let i=0;i<5;i++){if(offset>=packet.length)fail('Truncated protobuf');const c=packet[offset++];n+=(c&127)*factor;if(!(c&128))return n;factor*=128;}fail('Oversized protobuf integer');};
  while(offset<packet.length){
    const tag=read(),key=Math.floor(tag/8),wire=tag%8;
    if(!allowed.includes(key)||result.has(key))fail('Unexpected or duplicate protobuf field');
    if(wire===0)result.set(key,read());
    else if(wire===2){const length=read();if(length>MAX_PACKET||offset+length>packet.length)fail('Truncated protobuf bytes');result.set(key,packet.slice(offset,offset+length));offset+=length;}
    else fail('Unsupported protobuf wire type');
  }
  return result;
}
export function session(message,payload) {return join(field(2,2),field(12,join(field(1,message),field(message===0?20:22,payload))));}
function response(packet,message,member) {
  const outer=fields(packet,[2,12]);if(outer.get(2)!==2||!(outer.get(12) instanceof Uint8Array))fail('Security 2 is required');
  const inner=fields(outer.get(12),[1,member]);if(inner.get(1)!==message||!(inner.get(member) instanceof Uint8Array))fail('Unexpected handshake response');
  const body=fields(inner.get(member),[1,2,3]);if((body.get(1)??0)!==0)fail('Ownership authentication was rejected');return body;
}

export class Security2Client {
  #crypto;#random;#username;#password;#stage='new';#a=0n;#A=0n;#proof=null;#keyBytes=null;#key=null;#nonce=null;#busy=false;
  constructor(username,password,{cryptoProvider=globalThis.crypto,random}={}) {
    if(!cryptoProvider?.subtle||!cryptoProvider?.getRandomValues)fail('Secure browser cryptography is unavailable');
    if(typeof username!=='string'||typeof password!=='string'||username.length>64||password.length>128||!username||!password)fail('Invalid ownership input');
    this.#crypto=cryptoProvider;this.#random=random??(n=>cryptoProvider.getRandomValues(new Uint8Array(n)));this.#username=username;this.#password=password;
  }
  async #hash(...parts){return new Uint8Array(await this.#crypto.subtle.digest('SHA-512',join(...parts)));}
  async start() {
    if(this.#stage!=='new')fail('Start a new authentication session');
    try{
      // Match official v5.5.5 reference: avoid leading-zero A ambiguity.
      for(let i=0;i<128;i++){const entropy=this.#random(32);if(!(entropy instanceof Uint8Array)||entropy.length!==32)fail('Invalid random source');entropy[0]|=0x80;this.#a=integer(entropy);entropy.fill(0);this.#A=modPow(G,this.#a);if(intBytes(this.#A).length===384)break;if(i===127)fail('Unable to create SRP public key');}
      this.#stage='challenge';return session(0,join(field(1,encoder.encode(this.#username)),field(2,intBytes(this.#A))));
    }catch(error){this.destroy();throw error;}
  }
  async challenge(packet) {
    if(this.#stage!=='challenge')fail('Unexpected authentication step');
    try{
      const body=response(packet,1,21),Bbytes=body.get(2),salt=body.get(3);
      // esp_srp serializes server B at its minimal MPI width (occasionally383
      // bytes), while its proof path requires our client A to remain384 bytes.
      if(!(Bbytes instanceof Uint8Array)||Bbytes.length<1||Bbytes.length>384||!(salt instanceof Uint8Array)||salt.length<1||salt.length>32)fail('Invalid SRP challenge length');
      // Python strips leading-zero salt octets; SDK C hashes raw salt. M1 uses
      // a canonical fixed fixture and rejects this unreviewed encoding.
      if(salt[0]===0)fail('Unsupported leading-zero SRP salt in this prototype');
      const B=integer(Bbytes);if(B<=0n||B>=N)fail('Invalid SRP server key');
      const k=integer(await this.#hash(padded(N),padded(G))),u=integer(await this.#hash(padded(this.#A),padded(B)));if(u===0n)fail('Invalid SRP challenge');
      const identityHash=await this.#hash(encoder.encode(this.#username+':'+this.#password));
      if(identityHash[0]===0)fail('Unsupported SRP credential encoding in this prototype');
      const inner=integer(identityHash);
      const x=integer(await this.#hash(intBytes(integer(salt)),intBytes(inner)));
      const S=modPow(B-k*modPow(G,x),this.#a+u*x);this.#keyBytes=await this.#hash(intBytes(S));
      const hn=await this.#hash(intBytes(N)),hg=await this.#hash(padded(G));const nxg=hn.map((v,i)=>v^hg[i]);
      const M=await this.#hash(nxg,await this.#hash(encoder.encode(this.#username)),intBytes(integer(salt)),intBytes(this.#A),intBytes(B),this.#keyBytes);
      this.#proof=await this.#hash(intBytes(this.#A),M,this.#keyBytes);
      this.#key=await this.#crypto.subtle.importKey('raw',this.#keyBytes.slice(0,32),{name:'AES-GCM'},false,['encrypt','decrypt']);
      if(this.#stage!=='challenge')fail('Authentication session was closed');
      this.#a=0n;this.#password='';this.#stage='proof';return session(2,field(1,M));
    }catch(error){this.destroy();throw error;}
  }
  finish(packet) {
    if(this.#stage!=='proof')fail('Unexpected authentication step');
    try{
      const body=response(packet,3,23),proof=body.get(2),nonce=body.get(3);
      if(!(proof instanceof Uint8Array)||proof.length!==64||!equal(proof,this.#proof))fail('Device ownership proof failed');
      if(!(nonce instanceof Uint8Array)||nonce.length!==12)fail('Security 2 patch 1 nonce is required');
      this.#nonce=nonce.slice();this.#proof.fill(0);this.#proof=null;this.#keyBytes.fill(0);this.#keyBytes=null;this.#username='';this.#stage='ready';
    }catch(error){this.destroy();throw error;}
  }
  async #crypt(data,decrypt) {
    if(this.#stage!=='ready'||this.#busy)fail('Secure session is not ready');
    this.#busy=true;
    try{
      bytes(data);if(!decrypt&&data.length>MAX_PACKET-16)fail('Encrypted packet would be too large');if(decrypt&&data.length<16)fail('Truncated encrypted response');
      const view=new DataView(this.#nonce.buffer),counter=view.getUint32(8,false);if(counter===0xffffffff)fail('Session counter exhausted');
      const result=await this.#crypto.subtle[decrypt?'decrypt':'encrypt']({name:'AES-GCM',iv:this.#nonce,tagLength:128},this.#key,data);
      if(this.#stage!=='ready')fail('Secure session was closed');
      view.setUint32(8,counter+1,false);return new Uint8Array(result);
    }catch(error){this.destroy();throw new ProtocolError(decrypt?'Encrypted response failed verification':error.message);}
    finally{this.#busy=false;}
  }
  encrypt(data){return this.#crypt(data,false);}
  decrypt(data){return this.#crypt(data,true);}
  destroy(){this.#stage='closed';this.#a=0n;this.#A=0n;this.#username='';this.#password='';this.#proof?.fill(0);this.#keyBytes?.fill(0);this.#nonce?.fill(0);this.#proof=this.#keyBytes=this.#nonce=this.#key=null;}
}

// BEGIN OWNER LOGIN RECORD V1
// Canonical COW2 ownership parameters for the existing Security2 server. This
// reuses the pinned SRP3072/SHA512 primitives above; it is not another cipher,
// authentication protocol or password transport. The CRC detects corruption
// only. The caller must clear the returned record after staged delivery.
export class OwnerRecordError extends ProtocolError {
  constructor(code,message){super(message);this.code=code;}
}
export function validateOwnerLogin(username,password,{newPassword=false}={}){
  if(typeof username!=='string'||!/^[A-Za-z0-9._-]{1,32}$/.test(username))
    throw new OwnerRecordError('username','Use a username of 1–32 letters, numbers, dots, dashes or underscores.');
  if(typeof password!=='string'||!password||encoder.encode(password).length>128||
     new TextDecoder('utf-8',{fatal:true}).decode(encoder.encode(password))!==password||/[\x00-\x1f\x7f]/.test(password)||
     (newPassword&&encoder.encode(password).length<12))
    throw new OwnerRecordError('password',newPassword?'Choose a password of 12–128 UTF-8 bytes, without control characters.':'Enter your saved password (up to 128 UTF-8 bytes).');
}
export async function createOwnerRecord({device,username,password,cryptoProvider=globalThis.crypto,random,cancelled=()=>false}={}){
  validateOwnerLogin(username,password,{newPassword:true});
  if(typeof device!=='string'||!/^[0-9a-f]{12}$/.test(device))fail('The authenticated device identity is invalid.');
  if(!cryptoProvider?.subtle||!cryptoProvider?.getRandomValues)fail('Secure browser cryptography is unavailable');
  const alive=()=>{if(cancelled())fail('Login preparation was cancelled. Reconnect before trying again.');};
  const sample=random??(length=>cryptoProvider.getRandomValues(new Uint8Array(length)));
  let identity=encoder.encode(username+':'+password),identityHash=null,salt=null,verifier=null,combined=null,digest=null,record=null;
  password='';
  try{
    alive();identityHash=new Uint8Array(await cryptoProvider.subtle.digest('SHA-512',identity));identity.fill(0);identity=null;alive();
    // The official Python reference converts this inner digest to a minimal
    // MPI width, while the pinned SDK hashes all 64 octets. Existing clients
    // explicitly reject a leading zero. Never silently choose different math
    // or change a user's password to hide this compatibility limit.
    if(identityHash[0]===0)throw new OwnerRecordError('encoding','This username/password combination is incompatible with the installed Security2 encoding. Choose a different password.');
    for(let attempt=0;attempt<128;++attempt){
      alive();const entropy=sample(16);
      if(!(entropy instanceof Uint8Array)||entropy.length!==16)fail('The secure random source returned invalid salt.');
      salt=entropy;
      if(!salt[0]){salt.fill(0);salt=null;continue;}
      combined=join(salt,identityHash);digest=new Uint8Array(await cryptoProvider.subtle.digest('SHA-512',combined));combined.fill(0);combined=null;alive();
      const x=integer(digest);digest.fill(0);digest=null;verifier=intBytes(modPow(G,x));
      if(verifier.length!==384){verifier.fill(0);verifier=null;salt.fill(0);salt=null;continue;}
      record=new Uint8Array(432);record.set(encoder.encode('COW2'));record[4]=2;
      record.set(Uint8Array.from(device.match(/../g),value=>parseInt(value,16)),6);record.set(salt,12);record.set(verifier,28);
      let crc=0xffffffff;for(let i=0;i<428;++i){crc^=record[i];for(let bit=0;bit<8;++bit)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}
      new DataView(record.buffer).setUint32(428,(~crc)>>>0,true);alive();
      const result=record;record=null;return result;
    }
    fail('Could not create canonical Security2 parameters. No login change was saved.');
  }finally{identity?.fill(0);identityHash?.fill(0);salt?.fill(0);verifier?.fill(0);combined?.fill(0);digest?.fill(0);record?.fill(0);username='';password='';}
}
// END OWNER LOGIN RECORD V1

// Full upstream license retained in this served module (five-file runtime):
/*

                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

   END OF TERMS AND CONDITIONS

   APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

   Copyright [yyyy] [name of copyright owner]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/
