#include "probe_stubs/common.h"
#include "public_wifi_probe.h"
#include <iostream>
using namespace pro::public_wifi;
template<class Predicate>void until(Predicate ready){
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  while(!ready()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
void reap(){until([]{return probeReap();});assert(probeSnapshot().phase==ProbePhase::Stopped&&fake::tasks.empty()&&fake::allocations.empty());}
int main(){
  unsigned groups=0;
  for(int allocation=0;allocation<2;++allocation){fake::failAllocation=allocation;assert(!probeStart());
    assert(probeSnapshot().phase==ProbePhase::Failed&&fake::allocations.empty()&&probe_fake::loads==0);reap();}
  ++groups;
  fake::failCreate=true;assert(!probeStart());assert(probeSnapshot().phase==ProbePhase::Failed);reap();++groups;
  fake::duringCreate=[]{probeCancel();std::this_thread::sleep_for(std::chrono::milliseconds(10));assert(probe_fake::loads==0);};
  assert(probeStart());until([]{return probeSnapshot().phase==ProbePhase::Drained;});
  assert(!probeSnapshot().ok&&probeSnapshot().code==-2010&&probe_fake::loads==0);reap();fake::duringCreate={};++groups;
  probe_fake::hold=true;fake::holdPark=true;probe_fake::entered=false;
  unsigned cleanups=0;fake::onEvent=[&](const std::string& name){
    if(name=="probe TLS destroyed"||name=="probe client destroyed"||name=="probe config destroyed"){
      assert(probeSnapshot().phase==ProbePhase::Running);++cleanups;
    }
  };
  assert(probeStart());until([]{return probe_fake::entered.load();});
  assert(!probeStart()&&!probeReap());probeCancel();
  assert(probeSnapshot().phase==ProbePhase::Running&&cleanups==0);fake::release(probe_fake::hold);
  until([]{return probeSnapshot().phase==ProbePhase::Drained;});assert(cleanups>=4&&!probeSnapshot().ok&&probeSnapshot().code==-2010);
  assert(!probeReap()&&fake::allocations.size()==2);fake::release(fake::holdPark);
  fake::hideSuspended=true;assert(!probeReap());fake::hideSuspended=false;
  FakeTask stranger;fake::current=&stranger;assert(!probeStart()&&!probeReap());fake::current=&fake::owner;
  reap();const auto queries=fake::queries.load();assert(probeReap());assert(fake::queries==queries);fake::onEvent={};++groups;
  probe_fake::valid=false;const auto calls=probe_fake::calls;assert(probeStart());until([]{return probeSnapshot().phase==ProbePhase::Drained;});
  assert(!probeSnapshot().ok&&probeSnapshot().code==-2301&&probe_fake::calls==calls);reap();probe_fake::valid=true;++groups;
  for(unsigned i=0;i<20;++i){assert(probeStart());until([]{return probeSnapshot().phase==ProbePhase::Drained;});
    assert(probeSnapshot().ok&&probeSnapshot().code==207);reap();}++groups;
  pro::owned_guest::Uplink uplink;assert(readUplink(uplink)&&uplink.ip==0x0a000002&&uplink.mask==0xffffff00&&uplink.gateway==0x0a000001&&uplink.dns==0x08080808);
  probe_fake::connected=false;assert(!readUplink(uplink));probe_fake::connected=true;probe_fake::have_netif=false;assert(!readUplink(uplink));++groups;
  std::cout<<groups<<" actual probe-task adapter groups passed; configs/TLS synthetic, no NVS/cloud/device access\n";
}
