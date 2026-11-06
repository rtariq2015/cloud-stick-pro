// Include the actual selected old/new translation unit. Private state access
// below injects only the otherwise unreachable counter-exhaustion boundary.
#include "route_under_test.hpp"
#include <iostream>
using namespace pro::owned_guest;
void tick(){route_fake::now+=10;route_fake::pump();poll(route_fake::now);route_fake::pump();}
void drive(Phase wanted){for(unsigned i=0;i<1000&&status().phase!=wanted;++i)tick();assert(status().phase==wanted);}
void prepareRestart(bool racing=false){
  Request request{"synthetic-ap","synthetic-private-key",{route_fake::ip,route_fake::mask,route_fake::gateway,route_fake::dns},300000};
  assert(start(request));route_fake::pump();route_fake::now+=10;poll(route_fake::now);
  // AP.create has configured/read back private WPA2/deauthenticated clients;
  // deliver its startup restart before the ready barrier, never after Active.
  assert(route_fake::events.size()>=2);
  const auto barrier=route_fake::events.back();route_fake::events.pop_back();
  route_fake::emit(WIFI_EVENT_AP_STOP);route_fake::emit(WIFI_EVENT_AP_START);
  route_fake::events.push_back(barrier);route_fake::pump();
#ifdef FIXED_ROUTE_EPOCH
  if(racing){
    const auto enables=route_fake::enableCalls;
    route_fake::modeReadHook=[](){
      route_fake::modeReadHook=nullptr;
      route_fake::emit(WIFI_EVENT_AP_STOP);route_fake::emit(WIFI_EVENT_AP_START);route_fake::pump();
    };
    tick();assert(status().phase==Phase::Stopping&&!route_fake::napt&&route_fake::enableCalls==enables);return;
  }
#else
  (void)racing;
#endif
  tick();
  assert(status().phase==Phase::Active);
  assert(route_fake::napt&&route_fake::enableCalls&&route_fake::sockets.size()==2);
}
void clean(Error error){
  drive(Phase::Stopped);assert(status().error==error&&status().station_preserved&&!status().resources_retained);
  assert(route_fake::buffers.empty()&&route_fake::sockets.empty()&&route_fake::handlers.empty());
}
int main(){
  for(unsigned i=0;i<8;++i){
    prepareRestart();
    route_fake::emit(WIFI_EVENT_AP_STOP);route_fake::emit(WIFI_EVENT_AP_START);tick();
    assert(status().phase==Phase::Stopping);clean(Error::ApProof);
  }
#ifdef FIXED_ROUTE_EPOCH
  prepareRestart(true);clean(Error::ApProof);
  prepareRestart();
  // A stop racing a startup snapshot is not erased by admission. The next
  // liveness read compares the captured stop fence against the new counter.
  const auto captured=pro::owned_guest::admitted_stop_epoch;
  route_fake::emit(WIFI_EVENT_AP_STOP);route_fake::emit(WIFI_EVENT_AP_START);route_fake::pump();
  assert(pro::owned_guest::admitted_stop_epoch==captured&&!pro::owned_guest::apLive());
  tick();clean(Error::ApProof);
  prepareRestart();
  pro::owned_guest::events.ap_epoch.store(UINT32_MAX);
  route_fake::emit(WIFI_EVENT_AP_STOP);route_fake::emit(WIFI_EVENT_AP_START);route_fake::pump();
  assert(pro::owned_guest::events.ap_epoch_exhausted.load());
  assert(!pro::owned_guest::apLive());tick();clean(Error::ApProof);
#endif
  assert(route_fake::allocations==route_fake::frees);
  std::cout<<"PASS controlled startup restarts; every later STOP remains terminal; bounded cleanup\n";
}
