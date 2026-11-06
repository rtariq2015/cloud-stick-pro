#include "public_wifi_assistance.h"
#include <cassert>
#include <iostream>
using namespace pro::public_wifi;
namespace {
Authority owner(){return {7,1,9,true,true,false};}
Binding binding(){return {1,2,true};}
Observation observation(){Observation o;o.binding=binding();o.window=1;o.free=500000;o.largest=250000;
 o.ble_active=o.connected=o.auth_matches=o.worker_reclaimed=true;return o;}
void prepared(Coordinator& c,uint32_t at){assert(c.begin(owner(),binding(),10,at)==Error::None);}
void handoff(Coordinator& c,uint32_t at){prepared(c,at-1);assert(c.handoff(owner(),binding(),c.snapshot(at).grant,11,at)==Error::None);}
void check(Coordinator& c,Phase phase,uint32_t at,uint32_t duration){
 const auto stale=c.snapshot(at-1);assert(stale.phase==phase&&stale.error==Error::None);
 assert(stale.remaining_ms==duration);assert(c.snapshot(at).remaining_ms==duration);
 assert(c.snapshot(at+1).remaining_ms==duration-1);
}
}
int main(){
 unsigned groups=0;
 {
  Coordinator c;prepared(c,1000);check(c,Phase::Prepared,1000,Coordinator::PrepareMs);
  auto o=observation();assert(c.poll(o,999)==Action::None);
  assert(c.snapshot(1001).phase==Phase::Prepared&&c.snapshot(1001).error==Error::None);
  assert(c.handoff(owner(),binding(),c.snapshot(1001).grant,11,1002)==Error::None);
  check(c,Phase::AwaitDisconnect,1002,Coordinator::HandoffMs);
  assert(c.poll(o,1001)==Action::None&&c.snapshot(1003).error==Error::None);
 }++groups;
 {
  Coordinator c;prepared(c,1000);const auto grant=c.snapshot(1000).grant;
  assert(c.handoff(owner(),binding(),grant,11,999)==Error::None);
  assert(c.snapshot(1000).phase==Phase::AwaitDisconnect&&c.snapshot(1000).error==Error::None);
 }++groups;
 {
  Coordinator c;const uint32_t at=UINT32_MAX-10;prepared(c,at);check(c,Phase::Prepared,at,Coordinator::PrepareMs);
  auto o=observation();assert(c.poll(o,at-1)==Action::None);assert(c.poll(o,at+Coordinator::PrepareMs-1)==Action::None);
  assert(c.snapshot(at+Coordinator::PrepareMs-1).phase==Phase::Prepared);
  assert(c.poll(o,at+Coordinator::PrepareMs)==Action::None);assert(c.snapshot(at+Coordinator::PrepareMs).error==Error::Expired);
 }++groups;
 {
  Coordinator c;prepared(c,100);auto o=observation();assert(c.poll(o,100+Coordinator::PrepareMs-1)==Action::None);
  assert(c.snapshot(100+Coordinator::PrepareMs-1).remaining_ms==1);
  assert(c.poll(o,100+Coordinator::PrepareMs)==Action::None);assert(c.snapshot(100+Coordinator::PrepareMs).error==Error::Expired);
  Coordinator d;handoff(d,200);assert(d.poll(o,200+Coordinator::HandoffMs-1)==Action::None);
  assert(d.snapshot(200+Coordinator::HandoffMs-1).remaining_ms==1);assert(d.poll(o,200+Coordinator::HandoffMs)==Action::None);
  assert(d.snapshot(200+Coordinator::HandoffMs).error==Error::Expired);
 }++groups;
 {
  Coordinator c;handoff(c,1000);auto o=observation();o.connected=false;o.result_returned=true;
  assert(c.poll(o,1001)==Action::CloseBle);check(c,Phase::ClosingBle,1001,Coordinator::BleMs);
  assert(c.poll(o,1000)==Action::None);assert(c.snapshot(1002).phase==Phase::ClosingBle);
  o.ble_active=false;o.ble_clean=true;assert(c.poll(o,1002)==Action::None);check(c,Phase::WaitingSta,1002,Coordinator::StaMs);
  assert(c.poll(o,1001)==Action::None);assert(c.snapshot(1003).phase==Phase::WaitingSta);
  o.sta_valid=true;assert(c.poll(o,1003)==Action::StartRoute);o.route=Route::Active;
  check(c,Phase::Routing,1003,Coordinator::RouteMs);assert(c.poll(o,1002)==Action::None);
  assert(c.snapshot(1004).phase==Phase::Routing);c.poll(o,1004);
  o.route=Route::Stopped;o.route_clean=true;c.poll(o,1005);check(c,Phase::WaitingProbe,1005,Coordinator::RecoveryMs);
  assert(c.poll(o,1006)==Action::StartProbe);check(c,Phase::Probing,1006,Coordinator::ProbeMs);
  assert(c.poll(o,1005)==Action::None);assert(c.snapshot(1007).phase==Phase::Probing);
  o.probe_done=o.probe_reclaimed=o.probe_ok=true;c.poll(o,1007);check(c,Phase::Recovering,1007,Coordinator::RecoveryMs);
  o.free=100000;assert(c.poll(o,1006)==Action::None&&c.snapshot(1008).phase==Phase::Recovering);
 }++groups;
 {
  Coordinator c;prepared(c,1000);auto o=observation();
  assert(c.poll(o,uint32_t(1000+0x80000000u))==Action::None);
  assert(c.snapshot(1001).phase==Phase::Prepared);assert(c.poll(o,1000+Coordinator::PrepareMs)==Action::None);
  assert(c.snapshot(0).error==Error::Expired);
 }++groups;
 std::cout<<groups<<" clock policy groups passed; stale samples do not expire; real deadlines/wrap preserved\n";
}
