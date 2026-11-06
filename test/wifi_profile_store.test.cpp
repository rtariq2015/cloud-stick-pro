#include "wifi_profile_store.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
using namespace pro::wifi_setup;
namespace {
size_t checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::abort();}}while(0)
struct Memory final:BlobStore {
  std::map<std::string,std::vector<uint8_t>> values;
  unsigned operation=0,fail=0,cut=0;
  bool fail_after=false,corrupt=false;
  Read read(const char* key,uint8_t* out,size_t cap,size_t& n) override {
    n=0;++operation;if(operation==cut)throw std::runtime_error("synthetic power cut");
    if(operation==fail)return Read::Error;
    auto it=values.find(key);if(it==values.end())return Read::Missing;
    n=it->second.size();if(n>cap)return Read::Error;
    std::memcpy(out,it->second.data(),n);return Read::Found;
  }
  bool write(const char* key,const uint8_t* p,size_t n) override {
    ++operation;if(operation==cut)throw std::runtime_error("synthetic power cut");
    if(operation==fail&&!fail_after)return false;
    values[key]={p,p+n};if(operation==fail&&corrupt&&!values[key].empty())values[key][0]^=1;
    return operation!=fail;
  }
};
bool yes(void*){return true;}
StoredNetworks fixture(uint64_t revision=2) {
  StoredNetworks s;s.revision=revision;s.count=8;s.selected=1;s.next_id=9;
  for(unsigned i=0;i<8;++i){auto& p=s.profiles[i];p.id=i+1;p.priority=i;p.enabled=true;
    auto name=std::string("CSP Test ")+std::to_string(i);std::memcpy(p.ssid.data(),name.data(),name.size());
    auto pass=std::string("fixture-secret-")+std::to_string(i);std::memcpy(p.secret.data(),pass.data(),pass.size());}
  return s;
}
bool equal(const StoredNetworks& a,const StoredNetworks& b){
  std::array<uint8_t,NetworkRecordSize> aa{},bb{};
  return encodeNetworks(a,aa.data())&&encodeNetworks(b,bb.data())&&aa==bb;
}
void codec(){
  const auto s=fixture();std::array<uint8_t,NetworkRecordSize> bytes{};StoredNetworks loaded;
  CHECK(encodeNetworks(s,bytes.data()));CHECK(decodeNetworks(bytes.data(),bytes.size(),loaded));CHECK(equal(s,loaded));
  for(size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=0x80;CHECK(!decodeNetworks(bad.data(),bad.size(),loaded));}
  for(size_t n=0;n<NetworkRecordSize;++n)CHECK(!decodeNetworks(bytes.data(),n,loaded));
  CHECK(!decodeNetworks(nullptr,NetworkRecordSize,loaded));CHECK(!decodeNetworks(bytes.data(),NetworkRecordSize+1,loaded));
  auto invalid=s;invalid.count=9;CHECK(!encodeNetworks(invalid,bytes.data()));
  invalid=s;invalid.profiles[1].ssid=invalid.profiles[0].ssid;CHECK(!validStored(invalid));
  invalid=s;invalid.profiles[0].ssid[31]='x';CHECK(!validStored(invalid));
  invalid=s;invalid.selected=77;CHECK(!validStored(invalid));
  invalid=s;invalid.profiles[0].enabled=false;CHECK(!validStored(invalid));
  std::mt19937 rng(0x43574632);
  for(unsigned k=0;k<100000;++k){std::array<uint8_t,NetworkRecordSize> bad{};for(auto& v:bad)v=rng();CHECK(!decodeNetworks(bad.data(),rng()%1000,loaded));}
}
void publishing(){
  Memory mem;ProfileStore store(mem);StoredNetworks read;
  CHECK(store.load(read)==Stored::Missing);
  auto a=fixture(),b=fixture(3);b.profiles[0].priority=99;
  CHECK(store.publish(a,1,yes,nullptr)==Published::Committed);
  CHECK(store.load(read)==Stored::Valid&&equal(a,read));
  CHECK(store.publish(b,1,yes,nullptr)==Published::Conflict);
  CHECK(store.publish(b,2,yes,nullptr)==Published::Committed);
  CHECK(store.load(read)==Stored::Valid&&equal(b,read));
  CHECK(mem.values.size()==3);
  // An unselected valid generation is not boot evidence of an explicit commit.
  Memory staged;std::array<uint8_t,NetworkRecordSize> blob{};CHECK(encodeNetworks(b,blob.data()));
  staged.values["a"]={blob.begin(),blob.end()};ProfileStore fresh(staged);
  CHECK(fresh.load(read)==Stored::Missing);
  mem.values["active"][0]^=1;CHECK(store.load(read)==Stored::Corrupt);
  CHECK(store.publish(fixture(4),3,yes,nullptr)==Published::Unknown);
}
void interruption(){
  Memory baseline;{ProfileStore s(baseline);CHECK(s.publish(fixture(),1,yes,nullptr)==Published::Committed);}
  const auto old=baseline.values;auto replacement=fixture(3);replacement.profiles[0].priority=99;
  for(unsigned fault=1;fault<=10;++fault)for(bool after:{false,true})for(bool corrupt:{false,true}){
    Memory m;m.values=old;m.fail=fault;m.fail_after=after;m.corrupt=corrupt;
    ProfileStore s(m);const auto result=s.publish(replacement,2,yes,nullptr);
    m.fail=0;m.operation=0;StoredNetworks actual;const auto load=s.load(actual);
    if(result==Published::Committed)CHECK(load==Stored::Valid&&equal(actual,replacement));
    if(result==Published::Unchanged||result==Published::Conflict||result==Published::Cancelled)
      CHECK(load==Stored::Valid&&equal(actual,fixture()));
    CHECK(result==Published::Unknown||load==Stored::Valid);
  }
  for(unsigned cut=1;cut<=9;++cut){
    Memory m;m.values=old;m.cut=cut;try{ProfileStore s(m);s.publish(replacement,2,yes,nullptr);}catch(const std::runtime_error&){}
    m.cut=0;m.operation=0;ProfileStore reboot(m);StoredNetworks loaded;
    CHECK(reboot.load(loaded)==Stored::Valid);CHECK(equal(loaded,fixture())||equal(loaded,replacement));
  }
  Memory m;m.values=old;ProfileStore s(m);unsigned callbacks=0;
  CHECK(s.publish(replacement,2,[](void* p){return ++*static_cast<unsigned*>(p)<2;},&callbacks)==Published::Cancelled);
  StoredNetworks loaded;CHECK(s.load(loaded)==Stored::Valid&&equal(loaded,fixture()));
  // Legacy/cloud/receipt keys are outside this storage interface and unchanged.
  CHECK(!m.values.count("config")&&!m.values.count("events")&&!m.values.count("receipts"));
}
void policyImport(){
  Candidate model;auto s=fixture();CHECK(model.loadTrusted(s));StoredNetworks read;
  CHECK(model.exportActive(read)&&equal(s,read));
  auto bad=s;bad.profiles[0].secret.fill('x');CHECK(!model.loadTrusted(bad));CHECK(model.exportActive(read)&&equal(s,read));
  const Access owner{123,Proof::DeviceOwner,true,false,true};uint64_t tx=0;
  CHECK(model.begin(owner,s.revision,0,tx)==Error::None);Ticket t;
  CHECK(model.startCheck(owner,tx,0,1,t)==Error::None);
  CHECK(model.finishCheck(owner,t,0,Error::LocalOnly)==Error::None);Snapshot snapshot;
  CHECK(model.snapshot(owner,0,true,snapshot)==Error::None);
  CHECK(model.commitInMemory(owner,tx,snapshot.edit,s.revision,0)==Error::CheckRequired);
  CHECK(model.preparePersistent(owner,tx,snapshot.edit,s.revision,0,false,read)==Error::CheckRequired);
  CHECK(model.preparePersistent(owner,tx,snapshot.edit,s.revision,0,true,read)==Error::None);
  CHECK(read.revision==3&&read.selected==1); // explicitly acknowledged local-only save
}
}
int main(){codec();publishing();interruption();policyImport();std::cout<<"PASS "<<checks<<" store/codec/fault checks; 100000 hostile records\n";}
