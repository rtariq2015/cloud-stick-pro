// SPDX-License-Identifier: MIT
#include "ble_wake.h"
#include <algorithm>
#include <cstring>
namespace pro::ble_wake {
namespace {
uint32_t crc(const uint8_t* bytes,size_t count){
  uint32_t value=~uint32_t(0);
  for(size_t i=0;i<count;++i){value^=bytes[i];for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320U&uint32_t(0-int(value&1)));}
  return ~value;
}
bool equal(const Nonce& a,const Nonce& b){uint8_t difference=0;for(size_t i=0;i<a.size();++i)difference|=a[i]^b[i];return difference==0;}
bool valid(const Pairing& p){return p.owner_revision&&p.key[0]==4;}
}
bool encode(const Pairing& p,Record& out){
  out.fill(0);if(!valid(p))return false;
  std::memcpy(out.data(),"CWK1",4);std::copy(p.device.begin(),p.device.end(),out.begin()+4);
  for(unsigned i=0;i<8;++i)out[10+i]=uint8_t(p.owner_revision>>(56-i*8));
  std::copy(p.key.begin(),p.key.end(),out.begin()+18);
  const auto sum=crc(out.data(),83);for(unsigned i=0;i<4;++i)out[83+i]=uint8_t(sum>>(24-i*8));return true;
}
bool decode(const Record& bytes,const std::array<uint8_t,6>& device,uint64_t revision,Pairing& out){
  out={};if(std::memcmp(bytes.data(),"CWK1",4)!=0||!std::equal(device.begin(),device.end(),bytes.begin()+4))return false;
  uint32_t sum=0;for(unsigned i=0;i<4;++i)sum=(sum<<8)|bytes[83+i];if(sum!=crc(bytes.data(),83))return false;
  Pairing p;p.device=device;for(unsigned i=0;i<8;++i)p.owner_revision=(p.owner_revision<<8)|bytes[10+i];
  std::copy(bytes.begin()+18,bytes.begin()+83,p.key.begin());if(p.owner_revision!=revision||!valid(p))return false;
  out=p;return true;
}
Message message(const Id& id,const Nonce& nonce){
  Message out{};constexpr char domain[]="CloudStickWake/v1";
  static_assert(sizeof domain==18,"Wake signature domain includes its NUL byte");
  std::copy(domain,domain+sizeof domain,out.begin());
  std::copy(id.begin(),id.end(),out.begin()+18);std::copy(nonce.begin(),nonce.end(),out.begin()+18+IdBytes);return out;
}
void Gate::clear(){pairing_={};id_={};nonce_={};bound_=pending_=attempted_once_=woken_once_=false;issued_=attempted_=woken_=0;}
void Gate::bind(const Pairing& pairing,const Id& id){clear();if(valid(pairing)){pairing_=pairing;id_=id;bound_=true;}}
Result Gate::challenge(uint32_t now,uint64_t revision,bool available,Random random,void* context,Nonce& out){
  out={};if(!paired(revision))return Result::Unavailable;
  if(!available||(attempted_once_&&uint32_t(now-attempted_)<RetryMs)||(woken_once_&&uint32_t(now-woken_)<WakeCooldownMs))return Result::Busy;
  if(!pending_||uint32_t(now-issued_)>=ChallengeMs){
    nonce_={};pending_=false;if(!random||!random(context,nonce_)){nonce_={};return Result::Unavailable;}issued_=now;pending_=true;
  }
  out=nonce_;return Result::Ready;
}
Result Gate::authorize(uint32_t now,uint64_t revision,bool available,const Nonce& nonce,const uint8_t* signature,size_t size,Verify verify,void* context){
  if(!paired(revision))return Result::Unavailable;
  if(!available||(attempted_once_&&uint32_t(now-attempted_)<RetryMs)||(woken_once_&&uint32_t(now-woken_)<WakeCooldownMs))return Result::Busy;
  if(!pending_||uint32_t(now-issued_)>=ChallengeMs){pending_=false;nonce_={};return Result::Expired;}
  if(!signature||size<8||size>72||!equal(nonce,nonce_))return Result::Denied;
  attempted_=now;attempted_once_=true;pending_=false;nonce_={};
  if(!verify||!verify(context,pairing_.key,message(id_,nonce),signature,size))return Result::Denied;
  woken_=now;woken_once_=true;return Result::Ready;
}
}
