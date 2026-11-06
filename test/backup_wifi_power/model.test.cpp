#include "backup_wifi_power.h"
#include <cassert>
#include <cstdio>
#include <vector>
using Power=pro::BackupWifiPower;
struct Fixture {
  int mode=1,readError=0,writeError=0;
  bool apply=true;unsigned reads=0,writes=0,failReadAt=0;
  std::vector<int> calls;
  static int read(void* value,int& out){
    auto& f=*static_cast<Fixture*>(value);++f.reads;f.calls.push_back(200);
    const int code=f.reads==f.failReadAt?-71:f.readError;
    if(!code)out=f.mode;return code;
  }
  static int write(void* value,int mode){
    auto& f=*static_cast<Fixture*>(value);++f.writes;f.calls.push_back(100+mode);
    if(!f.writeError&&f.apply)f.mode=mode;return f.writeError;
  }
  Power::Driver driver(){return {this,read,write};}
};
int main(){
  static_assert(sizeof(Power)<=16,"Power policy must stay bounded and allocation-free");
  unsigned cases=0;
  for(int previous:{0,1,2}){
    Fixture f;f.mode=previous;Power lease;
    assert(lease.acquire(f.driver())&&lease.owned()&&!lease.faulted()&&lease.previous()==previous&&f.mode==0);
    const auto captured=f.calls;assert(lease.acquire(f.driver())&&f.calls==captured);
    assert(!lease.release(f.driver(),false)&&lease.owned()&&f.calls==captured);
    assert(lease.release(f.driver(),true)&&!lease.owned()&&!lease.faulted()&&f.mode==previous);
    const auto released=f.calls;assert(lease.release(f.driver(),false)&&f.calls==released);++cases;
  }
  {Fixture f;Power lease;f.readError=-9;assert(!lease.acquire(f.driver()));assert(!lease.owned()&&lease.fault()==Power::Fault::ReadPrevious&&lease.error()==-9&&f.writes==0);++cases;}
  {Fixture f;Power lease;f.mode=99;assert(!lease.acquire(f.driver()));assert(!lease.owned()&&lease.fault()==Power::Fault::InvalidPrevious&&f.writes==0);++cases;}
  {Fixture f;Power lease;f.writeError=-11;assert(!lease.acquire(f.driver()));assert(lease.owned()&&lease.previous()==1&&lease.fault()==Power::Fault::SetAwake&&lease.error()==-11);++cases;}
  {Fixture f;Power lease;f.failReadAt=2;assert(!lease.acquire(f.driver()));assert(lease.owned()&&lease.fault()==Power::Fault::ReadAwake&&lease.error()==-71);++cases;}
  {Fixture f;Power lease;f.apply=false;assert(!lease.acquire(f.driver()));assert(lease.owned()&&lease.fault()==Power::Fault::AwakeMismatch);++cases;}
  for(int failure:{0,1,2}){
    Fixture f;f.mode=2;Power lease;assert(lease.acquire(f.driver()));
    if(failure==0)f.writeError=-12;
    if(failure==1)f.readError=-13;
    if(failure==2)f.apply=false;
    assert(!lease.release(f.driver(),true)&&lease.faulted()&&lease.owned()&&lease.previous()==2);
    assert(lease.fault()==(failure==0?Power::Fault::SetPrevious:failure==1?Power::Fault::ReadRestored:Power::Fault::RestoreMismatch));
    const auto rejected=f.calls;f.writeError=f.readError=0;f.apply=true;
    for(unsigned n=0;n<100;++n){assert(!lease.acquire(f.driver()));assert(!lease.release(f.driver(),true));}
    assert(f.calls==rejected);++cases;
  }
  {Power lease;assert(!lease.acquire({nullptr,nullptr,nullptr})&&lease.fault()==Power::Fault::InvalidDriver);++cases;}
  std::printf("PASS %u pure policy cases: exact previous modes, quiescence, failure latch and zero SDK retries\n",cases);
}
