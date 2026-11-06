#include "owner_record.h"
#include "wifi_setup_model.h"
#include "owner_group.h"
#include <cstring>
namespace pro::wifi_setup {
bool decodeOwner(const uint8_t* p,size_t n,const uint8_t* mac,OwnerRecord& out) {
  eraseSecretMemory(&out,sizeof out);
  if(!p||!mac||n!=OwnerRecordSize||std::memcmp(p,"COW2",4)||p[4]!=2||p[5]||
     std::memcmp(p+6,mac,6)||!p[12]||p[28]==0||std::memcmp(p+28,OwnerGroupPrime,384)>=0)return false;
  // Fixed 16-byte non-leading-zero salt and canonical full-width verifier match
  // the reviewed browser/reference encoding; enrollment generator verifies SRP.
  uint32_t crc=~0U;for(size_t i=0;i<428;++i){crc^=p[i];for(unsigned k=0;k<8;++k)crc=(crc>>1)^(0xedb88320U&uint32_t(-int32_t(crc&1)));}
  uint32_t stored=0;for(unsigned i=0;i<4;++i)stored|=uint32_t(p[428+i])<<(8*i);
  if(~crc!=stored)return false;
  for(size_t i=412;i<428;++i)if(p[i])return false;
  std::memcpy(out.mac.data(),p+6,6);std::memcpy(out.salt.data(),p+12,16);
  std::memcpy(out.verifier.data(),p+28,384);return true;
}
} // namespace pro::wifi_setup
