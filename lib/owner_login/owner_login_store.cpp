#include "owner_login_feature.h"
#if CSP_ENABLE_OWNER_LOGIN
#include "owner_login_store.h"
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <cstring>

namespace pro::owner_login {
namespace {
using pro::wifi_setup::Read;
bool valid(const Bytes& bytes,const uint8_t* mac){
  pro::wifi_setup::OwnerRecord checked;
  const bool okay=pro::wifi_setup::decodeOwner(bytes.value.data(),bytes.value.size(),mac,checked);
  pro::wifi_setup::eraseSecretMemory(&checked,sizeof checked);return okay;
}
bool same(const Current& base,const Bytes& current,size_t length){
  return length==RecordSize&&!std::memcmp(base.bytes.value.data(),current.value.data(),RecordSize);
}
}
Bytes::~Bytes(){pro::wifi_setup::eraseSecretMemory(value.data(),value.size());}
uint64_t fingerprint(const Bytes& bytes){
  uint8_t digest[32]{};
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  const int status=mbedtls_sha256(bytes.value.data(),bytes.value.size(),digest,0);
#else
  const int status=mbedtls_sha256_ret(bytes.value.data(),bytes.value.size(),digest,0);
#endif
  uint64_t revision=0;
  if(!status)for(size_t i=0;i<8;++i)revision=(revision<<8)|digest[i];
  pro::wifi_setup::eraseSecretMemory(digest,sizeof digest);
  return status?0:revision?revision:1;
}
Store::Store(pro::wifi_setup::BlobStore& blobs,const uint8_t* mac):blobs_(blobs),identity_(mac!=nullptr){
  if(mac)std::memcpy(mac_.data(),mac,mac_.size());
}
Loaded Store::load(Current& out){
  pro::wifi_setup::eraseSecretMemory(out.bytes.value.data(),out.bytes.value.size());out.revision=0;
  if(!identity_)return Loaded::Unavailable;
  size_t length=0;const auto state=blobs_.read("owner",out.bytes.value.data(),RecordSize,length);
  if(state!=Read::Found){
    pro::wifi_setup::eraseSecretMemory(out.bytes.value.data(),out.bytes.value.size());
    return state==Read::Missing?Loaded::Missing:Loaded::Unavailable;
  }
  if(length!=RecordSize||!valid(out.bytes,mac_.data())){
    pro::wifi_setup::eraseSecretMemory(out.bytes.value.data(),out.bytes.value.size());return Loaded::Corrupt;
  }
  out.revision=fingerprint(out.bytes);
  if(!out.revision){pro::wifi_setup::eraseSecretMemory(out.bytes.value.data(),out.bytes.value.size());return Loaded::Unavailable;}
  return Loaded::Ready;
}
Saved Store::publish(const Current& baseline,const Bytes& candidate,uint64_t expected,
    pro::wifi_setup::StillAuthorized authorized,void* auth_context,uint64_t& new_revision,
    BeforeWrite before_write,void* before_context){
  new_revision=0;
  if(!authorized||!authorized(auth_context))return Saved::Cancelled;
  if(!identity_||!baseline.revision||!expected||expected!=baseline.revision)return Saved::Conflict;
  if(!valid(baseline.bytes,mac_.data())||!valid(candidate,mac_.data()))return Saved::Invalid;
  const uint64_t old=fingerprint(baseline.bytes),next=fingerprint(candidate);
  if(!old||!next)return Saved::Unavailable;
  if(old!=baseline.revision)return Saved::Conflict;
  if(next==old)return Saved::Invalid;
  Bytes current;size_t length=0;auto state=blobs_.read("owner",current.value.data(),RecordSize,length);
  if(state==Read::Error)return Saved::Unavailable;
  if(state!=Read::Found||!same(baseline,current,length))return Saved::Conflict;
  // All owner mutation runs on the single owner loop. This final full-value
  // comparison also detects writers that did not preserve the fingerprint.
  length=0;state=blobs_.read("owner",current.value.data(),RecordSize,length);
  if(state==Read::Error)return Saved::Unavailable;
  if(state!=Read::Found||!same(baseline,current,length))return Saved::Conflict;
  if(!authorized(auth_context))return Saved::Cancelled;
  // Runtime arms its terminal command barrier here. A successful hook is not
  // undone after an attempted write: uncertain persistence requires review.
  if(before_write&&!before_write(before_context))return Saved::Cancelled;
  if(!blobs_.write("owner",candidate.value.data(),RecordSize))return Saved::Unknown;
  length=0;state=blobs_.read("owner",current.value.data(),RecordSize,length);
  if(state!=Read::Found||length!=RecordSize||std::memcmp(current.value.data(),candidate.value.data(),RecordSize))return Saved::Unknown;
  new_revision=next;return Saved::Committed;
}
} // namespace pro::owner_login
#endif
