#include <owner_login_store.h>
#include "owner_login_nvs.h"
#include <owner_group.h>
#include <nvs.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>
using namespace pro::owner_login;
using pro::wifi_setup::Read;
static int hash_mode=0;
extern "C" int __real_mbedtls_sha256_ret(const unsigned char*,size_t,unsigned char*,int);
extern "C" int __wrap_mbedtls_sha256_ret(const unsigned char* input,size_t length,unsigned char* output,int mode){
  assert(input&&length==RecordSize&&output&&mode==0);
  if(hash_mode==1){std::memset(output,0xa5,32);return -1;}
  if(hash_mode==2||hash_mode==3){std::memset(output,0,32);if(hash_mode==3)output[7]=7;return 0;}
  return __real_mbedtls_sha256_ret(input,length,output,mode);
}
namespace {
constexpr uint8_t Mac[6]={0,0x11,0x22,0x33,0x44,0x55};
unsigned groups=0;
void pass(const char* label){++groups;std::cout<<"PASS "<<label<<'\n';}
void crc(Bytes& bytes){uint32_t value=~0U;for(size_t i=0;i<428;++i){value^=bytes.value[i];for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320U&uint32_t(-int32_t(value&1)));}
  value=~value;for(unsigned i=0;i<4;++i)bytes.value[428+i]=uint8_t(value>>(8*i));}
Bytes fixture(uint8_t seed=1){Bytes bytes;auto& b=bytes.value;std::memcpy(b.data(),"COW2",4);b[4]=2;std::memcpy(b.data()+6,Mac,6);
  for(unsigned i=0;i<16;++i)b[12+i]=uint8_t(seed+i);b[28]=seed;for(unsigned i=1;i<384;++i)b[28+i]=uint8_t(seed+i);crc(bytes);return bytes;}
std::string raw(const Bytes& bytes){return {reinterpret_cast<const char*>(bytes.value.data()),bytes.value.size()};}
struct Fake final:pro::wifi_setup::BlobStore {
  std::string value=raw(fixture());bool present=true,write_ok=true,apply_write=true,short_readback=false,corrupt_readback=false;
  bool require_barrier=false,barrier=false;unsigned reads=0,writes=0,fail_read_at=0,mutate_at=0;
  std::vector<std::string> events;
  Read read(const char* key,uint8_t* out,size_t capacity,size_t& length)override{
    assert(key&&!std::strcmp(key,"owner")&&out&&capacity==RecordSize);++reads;events.emplace_back("read");length=0;
    if(reads==fail_read_at)return Read::Error;if(!present)return Read::Missing;
    if(reads==mutate_at)value=raw(fixture(3));length=value.size();std::memcpy(out,value.data(),std::min(length,capacity));
    if(writes&&short_readback)--length;if(writes&&corrupt_readback)out[RecordSize-1]^=1;return Read::Found;
  }
  bool write(const char* key,const uint8_t* bytes,size_t length)override{
    assert(key&&!std::strcmp(key,"owner")&&bytes&&length==RecordSize&&(!require_barrier||barrier));++writes;events.emplace_back("write");
    if(apply_write){value.assign(reinterpret_cast<const char*>(bytes),length);present=true;}return write_ok;
  }
};
struct Auth {Fake* fake=nullptr;unsigned calls=0,deny_at=0,hook_calls=0,reads_at_hook=0;bool hook_ok=true;};
bool authorized(void* context){auto& auth=*static_cast<Auth*>(context);if(auth.fake)auth.fake->events.emplace_back("auth");return ++auth.calls!=auth.deny_at;}
bool barrier(void* context){auto& auth=*static_cast<Auth*>(context);assert(auth.fake);auto& fake=*auth.fake;
  assert(auth.calls==2&&fake.events.back()=="auth");++auth.hook_calls;auth.reads_at_hook=fake.reads;fake.events.emplace_back("barrier");
  if(auth.hook_ok)fake.barrier=true;return auth.hook_ok;}
void resetNvs(const Bytes& bytes=fixture()){
  using namespace owner_nvs_fake;assert(!opened);blob=raw(bytes);pending.clear();present=namespace_present=true;
  apply_set_error=apply_commit_error=corrupt_readback=false;open_result=query_result=read_result=set_result=commit_result=ESP_OK;
  use_query_override=use_read_override=false;opens=closes=queries=reads=writes=commits=fail_read_at=fail_query_at=fail_open_at=0;
}
}
int main(int argc,char** argv){
  assert(argc==3);auto first=fixture(),next=fixture(2);const auto candidate=raw(next);
  assert(fingerprint(first)==std::stoull(argv[1])&&fingerprint(next)==std::stoull(argv[2]));
  assert(fingerprint(first)!=fingerprint(next));pass("actual SHA-256 full-record big-endian revisions match independent Python hashlib fixtures");
  {
    hash_mode=2;assert(fingerprint(first)==1);hash_mode=1;assert(fingerprint(first)==0);hash_mode=0;
    alignas(Bytes) std::array<unsigned char,sizeof(Bytes)> memory{};auto* value=new(memory.data())Bytes;
    value->value.fill(0xa5);value->~Bytes();for(auto byte:memory)assert(byte==0);
    pass("zero digest prefix maps to1; library failure remains0; Bytes destruction scrubs all432 bytes");
  }
  {
    Fake fake;Store store(fake,Mac);Current current;const auto original=fake.value;
    assert(store.load(current)==Loaded::Ready&&current.revision==fingerprint(first)&&fake.writes==0&&fake.value==original);
    fake.require_barrier=true;Auth auth{&fake};uint64_t applied=0;
    assert(store.publish(current,next,current.revision,authorized,&auth,applied,barrier,&auth)==Saved::Committed);
    assert(applied==fingerprint(next)&&fake.value==candidate&&fake.writes==1&&auth.hook_calls==1&&auth.reads_at_hook==3&&fake.barrier);
    assert(fake.events[fake.events.size()-3]=="barrier"&&fake.events[fake.events.size()-2]=="write"&&fake.events.back()=="read");
    Current after;assert(store.load(after)==Loaded::Ready&&after.revision==applied);pass("read-only load; full-CAS commit arms barrier exactly before one write and exact readback");
  }
  {
    Fake fake;Store store(fake,Mac);Current current;fake.present=false;
    assert(store.load(current)==Loaded::Missing&&current.revision==0);for(auto byte:current.bytes.value)assert(byte==0);
    current.bytes=first;current.revision=fingerprint(first);Auth auth;uint64_t applied=999;
    assert(store.publish(current,next,current.revision,authorized,&auth,applied)==Saved::Conflict&&fake.writes==0&&applied==0);
    fake.present=true;fake.value[0]^=1;assert(store.load(current)==Loaded::Corrupt&&current.revision==0&&fake.writes==0);
    assert(store.publish(current,next,1,authorized,&auth,applied)==Saved::Conflict&&fake.writes==0);
    pass("missing or corrupt owner is never first-installed; failed loads erase retained raw/revision");
  }
  {
    for(size_t index=0;index<RecordSize;++index){Fake fake;fake.value[index]^=1;Store store(fake,Mac);Current current;assert(store.load(current)==Loaded::Corrupt&&current.revision==0&&fake.writes==0);}
    Fake fake;Store wrong(fake,nullptr);Current current;assert(wrong.load(current)==Loaded::Unavailable&&fake.reads==0);
    uint8_t other[6];std::memcpy(other,Mac,6);++other[5];Store mismatch(fake,other);assert(mismatch.load(current)==Loaded::Corrupt);
    pass("every single-byte COW2 corruption and wrong/missing device identity fails closed");
  }
  for(unsigned bad=0;bad<6;++bad){
    Fake fake;Store store(fake,Mac);Current current;assert(store.load(current)==Loaded::Ready);auto invalid=next;
    if(bad==0)invalid.value[6]^=1;if(bad==1)invalid.value[12]=0;if(bad==2)invalid.value[28]=0;
    if(bad==3)std::memcpy(invalid.value.data()+28,pro::wifi_setup::OwnerGroupPrime,384);
    if(bad==4)invalid.value[412]=1;if(bad==5)invalid.value[4]=3;crc(invalid);
    Auth auth{&fake};uint64_t applied=9;const auto original=fake.value;
    assert(store.publish(current,invalid,current.revision,authorized,&auth,applied,barrier,&auth)==Saved::Invalid&&fake.value==original&&fake.writes==0&&auth.hook_calls==0&&applied==0);
  }pass("candidate MAC, salt, verifier/group, reserved and schema failures reject before write/barrier");
  {
    Fake fake;Store store(fake,Mac);Current current;assert(store.load(current)==Loaded::Ready);Auth auth{&fake};uint64_t applied=9;
    assert(store.publish(current,first,current.revision,authorized,&auth,applied,barrier,&auth)==Saved::Invalid&&fake.writes==0&&auth.hook_calls==0);
    hash_mode=3;assert(store.load(current)==Loaded::Ready&&current.revision==7);
    assert(store.publish(current,next,7,authorized,&auth,applied,barrier,&auth)==Saved::Invalid&&fake.writes==0&&applied==0);hash_mode=0;
    pass("same record and a simulated truncated-fingerprint collision cannot rotate ownership");
  }
  for(unsigned change=0;change<3;++change){
    Fake fake;Store store(fake,Mac);Current current;assert(store.load(current)==Loaded::Ready);Auth auth{&fake};uint64_t applied=9;
    const auto expected=current.revision;if(change==0)++current.revision;if(change==1)fake.value=raw(fixture(3));if(change==2)fake.mutate_at=fake.reads+2;
    assert(store.publish(current,next,expected,authorized,&auth,applied,barrier,&auth)==Saved::Conflict&&fake.writes==0&&auth.hook_calls==0&&applied==0);
  }pass("revision mismatch, first CAS change and final CAS change never arm or write");
  for(unsigned denial:{1u,2u}){
    Fake fake;Store store(fake,Mac);Current current;assert(store.load(current)==Loaded::Ready);const auto original=fake.value;
    Auth auth{&fake};auth.deny_at=denial;uint64_t applied=9;
    assert(store.publish(current,next,current.revision,authorized,&auth,applied,barrier,&auth)==Saved::Cancelled&&fake.value==original&&fake.writes==0&&auth.hook_calls==0&&applied==0);
  }pass("initial and final authorization cancellation preserve the old owner and skip barrier");
  {
    Fake fake;Store store(fake,Mac);Current current;assert(store.load(current)==Loaded::Ready);const auto original=fake.value;
    Auth auth{&fake};auth.hook_ok=false;uint64_t applied=9;
    assert(store.publish(current,next,current.revision,authorized,&auth,applied,barrier,&auth)==Saved::Cancelled&&fake.value==original&&fake.writes==0&&auth.hook_calls==1&&!fake.barrier&&applied==0);
    pass("barrier refusal occurs after final authorization/CAS and prevents NVS write");
  }
  for(unsigned failure=0;failure<5;++failure){
    Fake fake;Store store(fake,Mac);Current current;assert(store.load(current)==Loaded::Ready);fake.require_barrier=true;
    if(failure==0)fake.write_ok=false;if(failure==1){fake.write_ok=false;fake.apply_write=false;}
    if(failure==2)fake.short_readback=true;if(failure==3)fake.corrupt_readback=true;if(failure==4)fake.fail_read_at=fake.reads+3;
    Auth auth{&fake};uint64_t applied=9;
    assert(store.publish(current,next,current.revision,authorized,&auth,applied,barrier,&auth)==Saved::Unknown&&fake.writes==1&&auth.hook_calls==1&&fake.barrier&&applied==0);
  }pass("all attempted-write/short/corrupt/failed readback outcomes stay Unknown with barrier retained and no rollback");
  {
    Fake fake;Store store(fake,Mac);Current current;hash_mode=1;assert(store.load(current)==Loaded::Unavailable&&current.revision==0);hash_mode=0;
    assert(store.load(current)==Loaded::Ready);hash_mode=1;Auth auth;uint64_t applied=9;
    assert(store.publish(current,next,current.revision,authorized,&auth,applied)==Saved::Unavailable&&fake.writes==0&&applied==0);hash_mode=0;
    for(unsigned at:{1u,2u}){fake.fail_read_at=fake.reads+at;Auth check;assert(store.publish(current,next,current.revision,authorized,&check,applied)==Saved::Unavailable&&fake.writes==0);fake.fail_read_at=0;}
    pass("hash-library and either precommit read failure remain unavailable without mutation");
  }
  {
    resetNvs();OwnerLoginNvs adapter;Store store(adapter,Mac);Current current;
    assert(store.load(current)==Loaded::Ready&&owner_nvs_fake::writes==0&&!owner_nvs_fake::opened);
    Auth auth;uint64_t applied=9;assert(store.publish(current,next,current.revision,authorized,&auth,applied)==Saved::Committed);
    assert(applied==fingerprint(next)&&owner_nvs_fake::writes==1&&owner_nvs_fake::commits==1&&owner_nvs_fake::blob==candidate&&!owner_nvs_fake::opened);
    uint8_t out[RecordSize]{};size_t length=999;const auto opens=owner_nvs_fake::opens;
    assert(adapter.read("config",out,RecordSize,length)==Read::Error&&length==0&&!adapter.write("config",next.value.data(),RecordSize));
    assert(adapter.read("owner",out,RecordSize-1,length)==Read::Error&&!adapter.write("owner",next.value.data(),RecordSize-1)&&owner_nvs_fake::opens==opens);
    pass("actual NVS adapter only reads/writes exact432-byte csp-owner/owner with one SDK commit");
  }
  for(unsigned failure=0;failure<8;++failure){
    resetNvs();OwnerLoginNvs adapter;Store store(adapter,Mac);Current current;assert(store.load(current)==Loaded::Ready);
    using namespace owner_nvs_fake;
    if(failure==0){set_result=ESP_FAIL;apply_set_error=true;}if(failure==1){commit_result=ESP_FAIL;apply_commit_error=true;}
    if(failure==2)corrupt_readback=true;if(failure==3)fail_read_at=reads+3;
    if(failure==4)fail_open_at=opens+4;if(failure==5)fail_query_at=queries+3;
    if(failure==6)set_result=ESP_FAIL;
    if(failure==7)fail_open_at=opens+3;
    Auth auth;uint64_t applied=9;assert(store.publish(current,next,current.revision,authorized,&auth,applied)==Saved::Unknown);
    assert(writes==(failure==7?0u:1u)&&applied==0&&!opened&&commits==(failure==0||failure==6||failure==7?0u:1u));
  }pass("actual SDK set/commit uncertainty and postwrite open/query/read corruption never become durable success");
  for(unsigned failure=0;failure<8;++failure){
    resetNvs();OwnerLoginNvs adapter;Store store(adapter,Mac);Current current;using namespace owner_nvs_fake;
    if(failure==0)namespace_present=false;if(failure==1)present=false;if(failure==2)open_result=ESP_FAIL;
    if(failure==3)query_result=ESP_FAIL;if(failure==4)read_result=ESP_FAIL;
    if(failure==5){use_query_override=true;query_override=RecordSize+1;}
    if(failure==6){use_read_override=true;read_override=RecordSize-1;}
    if(failure==7){use_query_override=true;query_override=RecordSize-1;}
    const auto result=store.load(current);assert(result==(failure<2?Loaded::Missing:Loaded::Unavailable));
    assert(current.revision==0&&writes==0&&commits==0&&!opened);for(auto byte:current.bytes.value)assert(!byte);
  }pass("actual namespace/key absence, I/O and exact-size faults close handles and never initialize an owner");
  std::cout<<"Native sizes: Bytes="<<sizeof(Bytes)<<" Current="<<sizeof(Current)<<" Store="<<sizeof(Store)<<" NVS="<<sizeof(OwnerLoginNvs)<<'\n';
  std::cout<<"PASS "<<groups<<" owner-login storage groups; synthetic COW2 only; no device/network\n";
}
