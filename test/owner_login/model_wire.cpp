#include <owner_login_model.h>
#include <wifi_wire.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string>
#include <vector>
using namespace pro::owner_login;
using pro::wifi_setup::Proof;
bool parse(const std::string& raw,pro::wifi_setup::WireRequest& out){return pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),out);}
int main(int argc,char** argv){
  assert(argc==2);std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> fixture{std::istreambuf_iterator<char>(input),{}};assert(fixture.size()==432);
  uint8_t mac[]{0,0x11,0x22,0x33,0x44,0x55};Access owner{9,Proof::DeviceOwner,true,false,true};
  Candidate candidate;assert(candidate.begin(owner,1,87,100)==Error::None);
  std::array<uint8_t,432> out{};
  assert(candidate.prepare(owner,1,87,0,true,mac,101,out)==Error::Invalid);
  assert(candidate.stage(owner,1,1,0,fixture.data(),128,102)==Error::StaleTransaction);
  assert(candidate.stage(owner,1,0,1,fixture.data(),128,102)==Error::StaleTransaction);
  assert(candidate.stage(owner,1,0,0,fixture.data(),129,102)==Error::Invalid);
  uint32_t edit=0,offset=0;
  while(offset<fixture.size()){const size_t count=std::min(size_t(128),fixture.size()-offset);assert(candidate.stage(owner,1,edit,offset,fixture.data()+offset,count,103)==Error::None);offset+=count;++edit;}
  assert(candidate.received()==432&&candidate.edit()==4);
  assert(candidate.stage(owner,1,4,432,fixture.data(),1,103)==Error::Invalid);
  assert(candidate.prepare(owner,1,88,4,true,mac,104,out)==Error::StaleRevision);
  assert(candidate.prepare(owner,1,87,3,true,mac,104,out)==Error::StaleTransaction);
  assert(candidate.prepare(owner,1,87,4,false,mac,104,out)==Error::ConfirmationRequired);
  uint8_t other[]{0,0x11,0x22,0x33,0x44,0x56};assert(candidate.prepare(owner,1,87,4,true,other,104,out)==Error::Invalid);
  assert(candidate.prepare(owner,1,87,4,true,mac,104,out)==Error::None&&std::equal(out.begin(),out.end(),fixture.begin()));
  Access stale=owner;stale.session=10;assert(candidate.abort(stale,1)==Error::StaleTransaction);
  assert(candidate.abort(owner,1)==Error::None&&candidate.received()==0);
  Access denied=owner;denied.proof=Proof::SyntheticM1;assert(candidate.begin(denied,2,87,100)==Error::Denied);
  denied=owner;denied.workers_quiescent=false;assert(candidate.begin(denied,2,87,100)==Error::WorkersBusy);
  assert(candidate.begin(owner,2,87,100)==Error::None);assert(candidate.status(owner,99)==Error::None);
  assert(candidate.status(owner,120100)==Error::Expired&&candidate.received()==0);
  assert(candidate.begin(owner,3,87,UINT32_MAX-100)==Error::None);assert(candidate.status(owner,1)==Error::None);candidate.clear();
  Handoff handoff;assert(handoff.arm(7,9,2,100));assert(!handoff.arm(7,9,3,100));assert(!handoff.matches(7,10,2));
  handoff.committed(999);assert(handoff.durable()&&handoff.revision()==999);
  handoff.encrypted(7,10,2,100);assert(!handoff.acknowledged());handoff.encrypted(7,9,2,110);
  assert(!handoff.shouldClose(109,true)&&!handoff.shouldClose(1109,true)&&handoff.shouldClose(1110,true));
  assert(handoff.shouldClose(110,false));handoff.clear();assert(!handoff.active());
  assert(handoff.arm(7,9,2,100));handoff.uncertain();assert(handoff.fault()&&!handoff.shouldClose(5099,true)&&handoff.shouldClose(5100,true));
  pro::wifi_setup::WireRequest request;
#if CSP_ENABLE_OWNER_LOGIN
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"login_begin\"}",request));
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"login_stage\",\"transaction\":\"18446744073709551615\",\"edit\":4294967295,\"offset\":304,\"data\":\""+std::string(256,'a')+"\"}",request));
  assert(request.login_size==128&&request.cursor==304&&request.login_bytes[0]==0xaa);
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"login_commit\",\"transaction\":\"1\",\"revision\":\"87\",\"edit\":4,\"accept\":true}",request));
  assert(parse("{\"v\":2,\"id\":1,\"op\":\"login_abort\",\"transaction\":\"1\"}",request));
#else
  for(const auto* op:{"login_begin","login_stage","login_commit","login_abort"})assert(!parse(std::string("{\"v\":2,\"id\":1,\"op\":\"")+op+"\"}",request));
#endif
  for(const auto* bad:{
   "{\"v\":2,\"id\":1,\"op\":\"login_begin\",\"password\":\"plaintext\"}",
   "{\"v\":2,\"id\":1,\"op\":\"login_abort\",\"transaction\":\"0\"}",
   "{\"v\":2,\"id\":1,\"op\":\"login_stage\",\"transaction\":\"1\",\"edit\":0,\"offset\":432,\"data\":\"00\"}",
   "{\"v\":2,\"id\":1,\"op\":\"login_stage\",\"transaction\":\"1\",\"edit\":0,\"offset\":0,\"data\":\"0g\"}",
   "{\"v\":2,\"id\":1,\"op\":\"login_stage\",\"transaction\":\"1\",\"edit\":0,\"offset\":0,\"data\":\"0\"}",
   "{\"v\":2,\"id\":1,\"op\":\"login_stage\",\"transaction\":\"1\",\"edit\":0,\"offset\":0,\"data\":\"\"}",
   "{\"v\":2,\"id\":1,\"op\":\"login_stage\",\"transaction\":\"1\",\"edit\":0,\"offset\":0,\"data\":\"00\",\"data\":\"01\"}",
   "{\"v\":2,\"id\":1,\"op\":\"login_commit\",\"transaction\":\"1\",\"revision\":\"87\",\"edit\":4,\"accept\":\"true\"}"})assert(!parse(bad,request));
  std::cout<<"PASS model/wire/chunks/consent/window/handoff/default-off; feature="<<CSP_ENABLE_OWNER_LOGIN<<" sizes="<<sizeof(Candidate)<<","<<sizeof(Handoff)<<","<<sizeof(request)<<"\n";
}
