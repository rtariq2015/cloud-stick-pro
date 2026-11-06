// Actual owned route; radio/netif/events/sockets are controlled SDK adapters.
#include "stubs/platform.h"
#include "owned_guest_route.h"
#include <iostream>
using namespace pro::owned_guest;
namespace {
unsigned cases=0;
Request request(){return {"synthetic-ap","synthetic-private-key",{route_fake::ip,route_fake::mask,route_fake::gateway,route_fake::dns},300000};}
void tick(){route_fake::now+=10;route_fake::pump();poll(route_fake::now);route_fake::pump();}
void drive(Phase phase){for(unsigned i=0;i<1000&&status().phase!=phase;++i)tick();if(status().phase!=phase)std::cerr<<"drive wanted="<<unsigned(phase)<<" actual="<<unsigned(status().phase)<<" error="<<unsigned(status().error)<<" mode="<<route_fake::mode<<" up="<<route_fake::apUp<<" now="<<route_fake::now<<'\n';assert(status().phase==phase);}
void begin(){assert(start(request()));drive(Phase::Active);assert(route_fake::apUp&&route_fake::napt);}
void clean(Error expected){
 drive(Phase::Stopped);assert(status().error==expected&&status().station_preserved&&!status().resources_retained);
 assert(route_fake::connected&&route_fake::mode==WIFI_MODE_STA&&!route_fake::apUp&&!route_fake::napt);
 assert(route_fake::buffers.empty()&&route_fake::sockets.empty()&&route_fake::handlers.empty());
}
void normalStop(){assert(!stop());clean(Error::None);}
void pass(const char* text){++cases;std::cout<<"PASS "<<text<<std::endl;}
void stoppedAp(){
 begin();const auto ends=route_fake::ends,disabled=route_fake::disableCalls;
 route_fake::apUp=false;route_fake::mode=WIFI_MODE_STA;route_fake::emit(WIFI_EVENT_AP_STOP);tick();
 assert(status().phase==Phase::Stopping);assert(status().error==Error::ApProof&&status().resources_retained);
 clean(Error::ApProof);assert(route_fake::ends==ends+1&&route_fake::disableCalls==disabled+1);
 pass("AP-only stop exits Active and completes checked cleanup while the STA stays connected");
}
void downNetif(){
 begin();route_fake::apUp=false;tick();assert(status().phase==Phase::Stopping);
 assert(route_fake::mode==WIFI_MODE_APSTA&&route_fake::connected);clean(Error::ApProof);
 pass("AP netif-down without a callback fails closed through the existing verified stop path");
}
}
int main(int argc,char** argv){
 if(argc==2){const std::string mode=argv[1];if(mode=="stop")stoppedAp();else if(mode=="netif")downNetif();else assert(false);return 0;}
 // Events queued before ownership or during the pre-start drain belong to the
 // preceding generation. They must be drained before the new AP proof resets.
 route_fake::emit(WIFI_EVENT_AP_STOP);assert(start(request()));route_fake::emit(WIFI_EVENT_AP_STOP);
 drive(Phase::Active);assert(status().error==Error::None);normalStop();
 pass("pre-start and pre-barrier stale AP_STOP callbacks do not poison a fresh generation");
 begin();route_fake::emit(999);route_fake::events.push_back({"unrelated",WIFI_EVENT_AP_STOP,0});tick();
 assert(status().phase==Phase::Active&&status().error==Error::None);normalStop();
 pass("unrelated Wi-Fi events and another event base cannot terminate the AP route");
 stoppedAp();downNetif();
 begin();route_fake::emit(WIFI_EVENT_AP_STOP);route_fake::emit(WIFI_EVENT_AP_START);tick();
 assert(status().phase==Phase::Stopping);clean(Error::ApProof);
 pass("late AP_STOP remains terminal even if an unexpected AP_START follows it");
 // One tick completes configure and posts the ready barrier without publishing
 // Active. A later down netif or stop event cannot admit NAPT for that startup.
 for(bool event:{false,true}){
  const auto enables=route_fake::enableCalls;assert(start(request()));tick();assert(status().phase==Phase::Starting);
  if(event)route_fake::emit(WIFI_EVENT_AP_STOP);else route_fake::apUp=false;
  tick();assert(status().phase==Phase::Stopping&&route_fake::enableCalls==enables&&!route_fake::napt);
  clean(Error::ApProof);
 }
 pass("readiness barrier refuses stopped/down AP before sockets or NAPT are admitted");
 begin();const auto generation=status().generation;assert(!stop());
 // Expected teardown callbacks are proof, not a fresh route failure. Extra
 // queued callbacks drain before handlers/buffers are released.
 route_fake::emit(WIFI_EVENT_AP_STOP);clean(Error::None);
 route_fake::emit(WIFI_EVENT_AP_STOP);tick();assert(status().phase==Phase::Stopped&&status().error==Error::None);
 begin();assert(status().generation>generation);normalStop();
 pass("expected teardown and post-teardown AP_STOP are safe and a new generation can start");
 assert(route_fake::allocations==route_fake::frees);
 for(const auto& entry:route_fake::closeCalls)assert(entry.second==1);
 std::cout<<cases<<" AP liveness groups passed; unchanged uplink and no real device/network access\n";
}
