#include "wifi_profile_store.h"
#include <cstring>

namespace pro::wifi_setup {
namespace {
void put32(uint8_t* p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = v >> (8*i); }
void put64(uint8_t* p, uint64_t v) { for (unsigned i = 0; i < 8; ++i) p[i] = v >> (8*i); }
uint32_t get32(const uint8_t* p) { uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v; }
uint64_t get64(const uint8_t* p) { uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v; }
uint32_t crc(const uint8_t* p, size_t n) {
  uint32_t v=~0U;for(size_t i=0;i<n;++i){v^=p[i];for(unsigned j=0;j<8;++j)v=(v>>1)^(0xedb88320U&uint32_t(-int32_t(v&1)));}return ~v;
}
bool allZero(const uint8_t* p,size_t n){for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
}
bool encodeNetworks(const StoredNetworks& s, uint8_t* out) {
  if (!out || !validStored(s)) return false;
  std::memset(out,0,NetworkRecordSize);std::memcpy(out,"CWF2",4);
  out[4]=2;out[5]=s.count;put64(out+8,s.revision);put32(out+16,s.next_id);put32(out+20,s.selected);
  for(size_t i=0;i<s.count;++i){
    const auto& p=s.profiles[i];auto* b=out+24+i*104;
    put32(b,p.id);b[4]=uint8_t(p.security);b[5]=p.priority;b[6]=p.enabled?1:0;
    b[7]=std::strlen(p.ssid.data());b[8]=std::strlen(p.secret.data());
    std::memcpy(b+9,p.ssid.data(),b[7]);std::memcpy(b+41,p.secret.data(),b[8]);
  }
  put32(out+856,crc(out,856));return true;
}
bool decodeNetworks(const uint8_t* b,size_t n,StoredNetworks& s) {
  eraseSecretMemory(&s,sizeof s);
  if(!b||n!=NetworkRecordSize||std::memcmp(b,"CWF2",4)||b[4]!=2||b[5]>ProfileLimit||
     b[6]||b[7]||get32(b+856)!=crc(b,856))return false;
  s.revision=get64(b+8);s.next_id=get32(b+16);s.selected=get32(b+20);s.count=b[5];
  for(size_t i=0;i<ProfileLimit;++i){
    const auto* p=b+24+i*104;
    if(i>=s.count){if(!allZero(p,104))goto bad;continue;}
    if(p[4]>1||p[6]>1||!p[7]||p[7]>32||p[8]>63||
       !allZero(p+9+p[7],32-p[7])||!allZero(p+41+p[8],63-p[8]))goto bad;
    auto& out=s.profiles[i];out.id=get32(p);out.security=Security(p[4]);out.priority=p[5];out.enabled=p[6];
    std::memcpy(out.ssid.data(),p+9,p[7]);std::memcpy(out.secret.data(),p+41,p[8]);
    // Length-bearing fields must not contain embedded NUL bytes.
    if(std::strlen(out.ssid.data())!=p[7]||std::strlen(out.secret.data())!=p[8])goto bad;
  }
  if(validStored(s))return true;
bad: eraseSecretMemory(&s,sizeof s);return false;
}
ProfileStore::~ProfileStore(){eraseSecretMemory(image_.data(),image_.size());eraseSecretMemory(verify_.data(),verify_.size());}
Stored ProfileStore::loadCurrent(StoredNetworks& s,uint8_t& selected) {
  eraseSecretMemory(&s,sizeof s);size_t n=0;
  auto r=blobs_.read("active",marker_.data(),marker_.size(),n);
  if(r==Read::Missing)return Stored::Missing; // staged slots alone never activate
  if(r!=Read::Found)return Stored::IO;
  if(n!=SelectionSize||std::memcmp(marker_.data(),"CWS2",4)||marker_[4]>1||
     marker_[5]||marker_[6]||marker_[7]||get32(marker_.data()+20)!=crc(marker_.data(),20))return Stored::Corrupt;
  selected=marker_[4];r=blobs_.read(selected?"b":"a",verify_.data(),verify_.size(),n);
  if(r==Read::Error)return Stored::IO;
  if(r!=Read::Found||!decodeNetworks(verify_.data(),n,s)||s.revision!=get64(marker_.data()+8)||
     get32(marker_.data()+16)!=crc(verify_.data(),verify_.size())){eraseSecretMemory(&s,sizeof s);return Stored::Corrupt;}
  return Stored::Valid;
}
Stored ProfileStore::load(StoredNetworks& s) {
  uint8_t slot=0;const auto result=loadCurrent(s,slot);
  eraseSecretMemory(verify_.data(),verify_.size());return result;
}
Published ProfileStore::publish(const StoredNetworks& next,uint64_t expected,StillAuthorized authorized,void* ctx) {
  struct Clean {
    ProfileStore& self;
    ~Clean(){eraseSecretMemory(self.image_.data(),self.image_.size());eraseSecretMemory(self.verify_.data(),self.verify_.size());}
  } clean{*this};
  if(!authorized||!authorized(ctx))return Published::Cancelled;
  if(expected==UINT64_MAX||next.revision!=expected+1||!encodeNetworks(next,image_.data()))return Published::Conflict;
  StoredNetworks current;
  struct Wipe {StoredNetworks& value;~Wipe(){eraseSecretMemory(&value,sizeof value);}} wipe{current};
  uint8_t old_slot=1;const auto before=loadCurrent(current,old_slot);
  if(before==Stored::Missing){if(expected!=1)return Published::Conflict;}
  else if(before!=Stored::Valid)return Published::Unknown;
  else if(current.revision!=expected)return Published::Conflict;
  const uint8_t new_slot=old_slot^1;
  const char* key=new_slot?"b":"a";
  // Do not publish on any stage failure, including a false success or a short/
  // corrupted readback. SDK failure may have written an inactive blob; ignore it.
  if(!blobs_.write(key,image_.data(),image_.size()))return Published::Unchanged;
  size_t length=0;
  if(blobs_.read(key,verify_.data(),verify_.size(),length)!=Read::Found||
     length!=image_.size()||std::memcmp(image_.data(),verify_.data(),length))return Published::Unchanged;
  if(!authorized(ctx))return Published::Cancelled;
  marker_.fill(0);std::memcpy(marker_.data(),"CWS2",4);marker_[4]=new_slot;
  put64(marker_.data()+8,next.revision);put32(marker_.data()+16,crc(image_.data(),image_.size()));
  put32(marker_.data()+20,crc(marker_.data(),20));
  // Linearization point: one NVS key selects the verified immutable generation.
  // Always inspect selection after failure, but a readback cannot establish a
  // failed commit's durability. Unknown publication stays blocked for review.
  const bool publication_written=blobs_.write("active",marker_.data(),marker_.size());
  uint8_t found_slot=0;const auto after=loadCurrent(current,found_slot);
  if(!publication_written)return Published::Unknown;
  if(after==Stored::Valid&&found_slot==new_slot&&current.revision==next.revision&&
     encodeNetworks(current,verify_.data())&&std::memcmp(image_.data(),verify_.data(),image_.size())==0)
    return Published::Committed;
  if((before==Stored::Missing&&after==Stored::Missing)||
     (before==Stored::Valid&&after==Stored::Valid&&found_slot==old_slot&&current.revision==expected))
    return Published::Unchanged;
  return Published::Unknown; // deny further mutations; never format or guess
}
} // namespace pro::wifi_setup
