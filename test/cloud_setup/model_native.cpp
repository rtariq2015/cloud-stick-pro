#include <owner_cloud_model.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>
#include <type_traits>
using namespace pro::owner_cloud;
using pro::wifi_setup::Proof;
namespace {
unsigned cases=0;
Access owner(){return {7,Proof::DeviceOwner,true,false,true};}
void pass(){++cases;}
void fill(Candidate& model,uint32_t now=101){
  assert(model.stage(owner(),1,0,now,Field::Username,"synthetic-owner@example.invalid")==Error::None);
  assert(model.stage(owner(),1,1,now,Field::AppPassword,"synthetic-app-password")==Error::None);
  assert(model.stage(owner(),1,2,now,Field::Folder,"SyntheticDevice/QA")==Error::None);
}
}
int main(){
  static_assert(sizeof(Values)<=360,"Bounded cloud credential staging changed");
  for(const auto* text:{"CloudStickPro","Device-1/QA_2","A.b/c-d"})assert(validFolder(text));
  for(const auto* text:{"","/absolute","trailing/","a//b",".","..","a/../b","a/./b","%2e%2e","bad space","a?x","a#x","a\\b","_first"})assert(!validFolder(text));
  assert(validFolder(std::string(96,'A'))&&!validFolder(std::string(97,'A')));pass();
  assert(validCredential(std::string(128,'"'),false)&&validCredential(std::string(128,'\\'),false));
  assert(!validCredential({},false)&&!validCredential(std::string(129,'a'),false));
  assert(!validCredential("bad:user",true)&&validCredential("good:password",false));
  for(const auto& text:{std::string("bad\nvalue"),std::string("a\0b",3),std::string("\xc0\x80",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4)})assert(!validCredential(text,false));
  assert(validCredential("valid-\xc3\xa9",true));pass();
  for(auto proof:{Proof::None,Proof::LegacyShared,Proof::SyntheticM1}){
    Candidate m;Values initial;auto a=owner();a.proof=proof;assert(m.begin(a,1,1,1,100,initial)==Error::Denied);pass();
  }
  {Candidate m;Values initial;auto a=owner();a.physical_window=false;assert(m.begin(a,1,1,1,100,initial)==Error::Denied);a=owner();a.terminal_rejection=true;assert(m.begin(a,1,1,1,100,initial)==Error::Denied);a=owner();a.workers_quiescent=false;assert(m.begin(a,1,1,1,100,initial)==Error::WorkersBusy);pass();}
  {Candidate m;Values initial;assert(m.begin(owner(),1,2,1,100,initial)==Error::StaleRevision);assert(m.begin(owner(),UINT64_MAX,UINT64_MAX,1,100,initial)==Error::Exhausted);pass();}
  {
    Candidate m;Values initial,out;assert(m.begin(owner(),1,1,1,100,initial)==Error::None);fill(m);
    assert(m.edit()==3&&m.presence().has_username&&m.presence().has_password&&m.presence().has_folder);
    assert(m.prepare(owner(),1,1,3,102,false,out)==Error::ConfirmationRequired);
    assert(m.prepare(owner(),1,2,3,102,true,out)==Error::StaleRevision);
    assert(m.prepare(owner(),1,1,2,102,true,out)==Error::StaleTransaction);
    assert(m.prepare(owner(),1,1,3,102,true,out)==Error::None&&complete(out));pass();
    assert(m.stage(owner(),1,2,103,Field::Folder,"Different")==Error::StaleTransaction&&m.edit()==3);
    assert(m.stage(owner(),1,3,103,Field::Folder,"../escape")==Error::Invalid&&m.edit()==3);
    assert(m.abort(owner(),2)==Error::StaleTransaction&&m.editing());
    assert(m.abort(owner(),1)==Error::None&&!m.editing()&&!m.presence().has_password);pass();
  }
  {Candidate m;Values initial,out;assert(m.begin(owner(),1,1,1,100,initial)==Error::None);assert(m.prepare(owner(),1,1,0,101,true,out)==Error::Invalid);pass();}
  {Candidate m;Values initial;assert(m.begin(owner(),1,1,1,100,initial)==Error::None);fill(m);auto other=owner();other.session=8;assert(m.stage(other,1,3,102,Field::Folder,"Other")==Error::StaleTransaction);auto rejected=owner();rejected.terminal_rejection=true;assert(m.stage(rejected,1,3,102,Field::Folder,"Other")==Error::Denied);assert(!m.editing()&&!m.presence().has_password);pass();}
  {Candidate m;Values initial;assert(m.begin(owner(),1,1,1,100,initial)==Error::None);fill(m);assert(m.status(owner(),99)==Error::None);assert(m.status(owner(),120099)==Error::None);assert(m.status(owner(),120100)==Error::Expired&&!m.editing()&&!m.presence().has_password);assert(m.stage(owner(),1,3,120101,Field::Folder,"Late")==Error::Expired);pass();}
  {Candidate m;Values initial;const uint32_t start=UINT32_MAX-100;assert(m.begin(owner(),1,1,1,start,initial)==Error::None);assert(m.status(owner(),49)==Error::None);assert(m.status(owner(),start+LifetimeMs)==Error::Expired);pass();}
  {
    std::aligned_storage_t<sizeof(Values),alignof(Values)> storage;
    auto* value=new(&storage) Values;std::memset(value->username.data(),'u',value->username.size());std::memset(value->password.data(),'p',value->password.size());std::memset(value->folder.data(),'f',value->folder.size());
    value->~Values();const auto* bytes=reinterpret_cast<const unsigned char*>(&storage);for(size_t i=0;i<sizeof(Values);++i)assert(bytes[i]==0);pass();
  }
  std::printf("PASS %u pure owner-cloud cases; Values=%zu Candidate=%zu bytes on desktop; no device\n",cases,sizeof(Values),sizeof(Candidate));
}
