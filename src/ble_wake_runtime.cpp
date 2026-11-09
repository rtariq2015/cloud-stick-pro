// SPDX-License-Identifier: MIT
#include "ble_wake_runtime.h"
#if CSP_ENABLE_BLE_WIFI_WAKE
#include "wifi_setup_runtime.h"
#include "backup_worker.h"
#include "pro_device.h"
#include <nvs.h>
#include <esp_random.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <algorithm>
#include <cstring>
#if MBEDTLS_VERSION_MAJOR >= 3
#define CSP_WAKE_FIELD(ctx,field) ((ctx).MBEDTLS_PRIVATE(field))
#else
#define CSP_WAKE_FIELD(ctx,field) ((ctx).field)
#endif
namespace pro::ble_wake {
namespace {
Gate gate;
std::array<uint8_t,6> device{};
uint64_t revision=0;
bool uncertain=false;
bool digest(const uint8_t* input,size_t size,uint8_t* out){
#if MBEDTLS_VERSION_MAJOR >= 3
  return mbedtls_sha256(input,size,out,0)==0;
#else
  return mbedtls_sha256_ret(input,size,out,0)==0;
#endif
}
struct Curve {
  mbedtls_ecdsa_context value;
  Curve(){mbedtls_ecdsa_init(&value);}
  ~Curve(){mbedtls_ecdsa_free(&value);}
  bool load(const PublicKey& key){
    return mbedtls_ecp_group_load(&CSP_WAKE_FIELD(value,grp),MBEDTLS_ECP_DP_SECP256R1)==0&&
      mbedtls_ecp_point_read_binary(&CSP_WAKE_FIELD(value,grp),&CSP_WAKE_FIELD(value,Q),key.data(),key.size())==0&&
      mbedtls_ecp_check_pubkey(&CSP_WAKE_FIELD(value,grp),&CSP_WAKE_FIELD(value,Q))==0;
  }
};
bool publicIdentity(const Pairing& p,Id& out){
  Record bytes;std::array<uint8_t,32> hash{};
  if(!encode(p,bytes)||!digest(bytes.data(),83,hash.data()))return false;
  std::copy(hash.begin(),hash.begin()+out.size(),out.begin());return true;
}
bool random(void*,Nonce& out){esp_fill_random(out.data(),out.size());return true;}
bool verify(void*,const PublicKey& key,const Message& message,const uint8_t* signature,size_t size){
  Curve curve;std::array<uint8_t,32> hash{};
  return curve.load(key)&&digest(message.data(),message.size(),hash.data())&&
    mbedtls_ecdsa_read_signature(&curve.value,hash.data(),hash.size(),signature,size)==0;
}
bool read(Record& out){
  nvs_handle_t handle;if(nvs_open("csp-wake",NVS_READONLY,&handle)!=ESP_OK)return false;
  size_t length=out.size();const auto result=nvs_get_blob(handle,"phone",out.data(),&length);nvs_close(handle);return result==ESP_OK&&length==out.size();
}
}
void initialize(const std::array<uint8_t,6>& identity,uint64_t owner_revision){
  gate.clear();uncertain=false;device=identity;revision=owner_revision;if(!revision)return;
  Record bytes;Pairing stored;Id id;
  if(read(bytes)&&decode(bytes,device,revision,stored)&&publicIdentity(stored,id)){Curve curve;if(curve.load(stored.key))gate.bind(stored,id);}
}
// Owner change happens after verified close: revoke without reopening NVS
// or allocating a curve/cache in the just-recovered BLE arena.
void revoke(uint64_t owner_revision){gate.clear();revision=owner_revision;uncertain=false;}
bool paired(){return gate.paired(revision);}
Id identity(){return gate.id();}
bool pairPhone(const WifiSetupSession& session,const PublicKey& key,Id& out){
  out={};
  if(!revision||!ble_management_authorizes(session)||!wifi_setup_owner_login_info_allowed()||
     !maintenance_active()||backup_worker_snapshot().phase!=BackupWorkerPhase::Stopped)return false;
  Curve curve;if(!curve.load(key))return false;
  Pairing next;next.device=device;next.owner_revision=revision;next.key=key;
  Record encoded;Id id;if(!encode(next,encoded)||!publicIdentity(next,id))return false;
  Record current;
  if(!uncertain&&read(current)&&current==encoded){gate.bind(next,id);out=id;return true;}
  // Recheck the live authenticated generation immediately before the only write.
  if(!ble_management_authorizes(session)||!wifi_setup_owner_login_info_allowed())return false;
  nvs_handle_t handle;if(nvs_open("csp-wake",NVS_READWRITE,&handle)!=ESP_OK)return false;
  uncertain=true;
  const bool committed=nvs_set_blob(handle,"phone",encoded.data(),encoded.size())==ESP_OK&&nvs_commit(handle)==ESP_OK;
  nvs_close(handle);
  if(!committed||!read(current)||current!=encoded){gate.clear();return false;}
  uncertain=false;gate.bind(next,id);out=id;return true;
}
Result challenge(uint32_t now,bool available,Nonce& out){return gate.challenge(now,revision,available,random,nullptr,out);}
Result authorize(uint32_t now,bool available,const Nonce& nonce,const uint8_t* signature,size_t size){return gate.authorize(now,revision,available,nonce,signature,size,verify,nullptr);}
}
#endif
