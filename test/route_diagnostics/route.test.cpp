#include "stubs/platform.h"
#include "owned_guest_route.h"
#include <iostream>
using namespace pro::owned_guest;
void tick(){route_fake::now+=10;route_fake::pump();poll(route_fake::now);route_fake::pump();}
void drive(Phase phase){for(unsigned i=0;i<1000&&status().phase!=phase;++i)tick();assert(status().phase==phase);}
int main(int argc,char** argv){
  assert(argc==2);const std::string mode=argv[1];
  Request request{"synthetic-ap","synthetic-private-key",{route_fake::ip,route_fake::mask,route_fake::gateway,route_fake::dns},300000};
  assert(start(request));
  if(mode=="late-start"){
    // Deliver the pre-barrier, configure AP and withhold only AP_START while
    // delivering the readiness barrier. Diagnose the actual present policy;
    // this does not claim that the device's unknown Route9 has this cause.
    route_fake::pump();route_fake::now+=10;poll(route_fake::now);
    auto& queue=route_fake::events;
    queue.erase(std::remove_if(queue.begin(),queue.end(),[](const auto& event){return event.base==WIFI_EVENT&&event.id==WIFI_EVENT_AP_START;}),queue.end());
    route_fake::pump();tick();assert(status().phase==Phase::Stopping);
    const auto d=diagnostic();assert(d.failure_phase==unsigned(Phase::Starting)&&d.failure_step==3&&d.ap_proof==1);
  }else{
    drive(Phase::Active);assert(diagnostic().ap_proof==3);
    if(mode=="down")route_fake::apUp=false;
    else if(mode=="stop")route_fake::emit(WIFI_EVENT_AP_STOP);
    else assert(false);
    tick();assert(status().phase==Phase::Stopping);
    const auto d=diagnostic();assert(d.failure_phase==unsigned(Phase::Active));
    assert(d.ap_proof==(mode=="down"?2:7));
  }
  drive(Phase::Stopped);assert(status().error==Error::ApProof&&!status().resources_retained&&status().station_preserved);
  const auto saved=diagnostic();route_fake::current=reinterpret_cast<void*>(2);
  assert(diagnostic().ap_proof==0);route_fake::current=reinterpret_cast<void*>(1);assert(diagnostic().ap_proof==saved.ap_proof);
  assert(route_fake::buffers.empty()&&route_fake::handlers.empty()&&route_fake::sockets.empty());
  std::cout<<"PASS numeric route observation and existing fail-closed cleanup "<<mode<<'\n';
}
