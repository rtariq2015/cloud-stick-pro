#include "public_wifi_assistance.h"
#include <cassert>
#include <iostream>
using namespace pro::public_wifi;
namespace {
Authority owner(){return {7,1,9,true,true,false};}
Binding saved(){return {1,2,true};}
Observation observation(){Observation o;o.binding=saved();o.window=1;o.free=500000;o.largest=250000;
  o.ble_active=o.connected=o.auth_matches=o.worker_reclaimed=true;return o;}
void arm(Coordinator& c,uint32_t now=100){
  assert(c.begin(owner(),saved(),10,now)==Error::None);
  assert(c.handoff(owner(),saved(),c.snapshot(now).grant,11,now+1)==Error::None);
}
void close(Coordinator& c,Observation& o,uint32_t now=200){
  o.connected=false;o.result_returned=true;assert(c.poll(o,now)==Action::CloseBle);
  o.ble_active=false;o.ble_clean=true;assert(c.poll(o,now+1)==Action::None);
  assert(c.snapshot(now+1).phase==Phase::WaitingSta);
}
void route(Coordinator& c,Observation& o){
  arm(c);close(c,o);o.sta_valid=true;assert(c.poll(o,202)==Action::StartRoute);
  o.route=Route::Active;assert(c.poll(o,203)==Action::None);
}
void probe(Coordinator& c,Observation& o){
  route(c,o);o.route=Route::Stopped;o.route_clean=true;assert(c.poll(o,204)==Action::None);
  assert(c.poll(o,205)==Action::StartProbe);
}
}
int main(){
  unsigned groups=0;
  for(unsigned field=0;field<6;++field){Coordinator c;auto a=owner();
    if(field==0)a.session=0;if(field==1)a.generation=0;if(field==2)a.window=0;
    if(field==3)a.private_owner=false;if(field==4)a.physical_window=false;if(field==5)a.terminal=true;
    assert(c.begin(a,saved(),1,100)==Error::Denied);assert(!c.blocksAutomaticWork());}
  ++groups;
  {
    Coordinator c;assert(c.begin(owner(),saved(),10,100)==Error::None);const auto grant=c.snapshot(100).grant;
    assert(c.begin(owner(),saved(),10,200)==Error::Replay);
    auto other=owner();++other.generation;assert(c.handoff(other,saved(),grant,11,200)==Error::Denied);
    auto stale=saved();++stale.revision;assert(c.handoff(owner(),stale,grant,11,200)==Error::Stale);
    assert(c.handoff(owner(),saved(),grant+1,11,200)==Error::Stale);
    assert(c.handoff(owner(),saved(),grant,11,200)==Error::None);
    assert(c.handoff(owner(),saved(),grant,11,300)==Error::Replay);
    assert(c.handoff(owner(),saved(),grant,12,300)==Error::Stale);
    assert(c.snapshot(300).remaining_ms==Coordinator::HandoffMs-100);
  }++groups;
  {
    Coordinator c;arm(c);auto o=observation();o.result_returned=true;
    assert(c.poll(o,300)==Action::None); // Publishing a reply never closes an active connection.
    o.connected=false;o.result_returned=false;assert(c.poll(o,301)==Action::None);
    assert(c.snapshot(301).phase==Phase::Complete&&c.snapshot(301).error==Error::Cancelled);
  }++groups;
  {
    Coordinator c;arm(c);auto o=observation();o.connected=false;o.result_returned=true;o.terminal=true;
    assert(c.poll(o,300)==Action::None&&c.snapshot(300).error==Error::Denied);
    Coordinator d;arm(d);o=observation();o.ble_active=false;o.connected=false;o.result_returned=true;
    assert(d.poll(o,300)==Action::None&&d.snapshot(300).error==Error::Denied);
  }++groups;
  {
    Coordinator c;arm(c);auto o=observation();o.connected=false;o.result_returned=true;o.worker_reclaimed=false;
    assert(c.poll(o,300)==Action::None&&c.snapshot(300).phase==Phase::Blocked);
    assert(c.blocksAutomaticWork());
  }++groups;
  {
    Coordinator c;assert(c.begin(owner(),saved(),10,100)==Error::None);
    auto o=observation();++o.binding.revision;assert(c.poll(o,101)==Action::None);
    assert(c.snapshot(101).error==Error::Stale);
    Coordinator d;assert(d.begin(owner(),saved(),10,100)==Error::None);const auto ticket=d.snapshot(100).grant;
    assert(d.cancel(owner(),ticket,11,101)==Error::None&&!d.blocksAutomaticWork());
    assert(d.begin(owner(),saved(),10,102)==Error::Replay);
    assert(d.begin(owner(),saved(),12,102)==Error::None&&d.snapshot(102).grant!=ticket);
  }++groups;
  {
    Coordinator c;const uint32_t start=UINT32_MAX-100;assert(c.begin(owner(),saved(),10,start)==Error::None);
    auto o=observation();assert(c.poll(o,start+Coordinator::PrepareMs)==Action::None);
    assert(c.snapshot(0).error==Error::Expired);
    Coordinator d;arm(d,start);close(d,o,150);
    assert(d.snapshot(151).phase==Phase::WaitingSta);
  }++groups;
  {
    Coordinator c;arm(c);auto o=observation();o.connected=false;o.result_returned=true;
    assert(c.poll(o,200)==Action::CloseBle);o.ble_active=false;
    assert(c.poll(o,200+Coordinator::BleMs)==Action::None&&c.snapshot(0).phase==Phase::Blocked);
    Coordinator d;arm(d);o=observation();o.connected=false;o.result_returned=true;
    assert(d.poll(o,200)==Action::CloseBle);o.ble_failed=true;
    assert(d.poll(o,201)==Action::None&&d.snapshot(0).error==Error::Ble);
  }++groups;
  {
    Coordinator c;arm(c);auto o=observation();close(c,o);o.sta_valid=true;o.free=160*1024-1;
    assert(c.poll(o,202)==Action::None);o.free=500000;o.largest=64*1024-1;assert(c.poll(o,203)==Action::None);
    o.largest=250000;assert(c.poll(o,204)==Action::StartRoute);
    o.route=Route::Blocked;assert(c.poll(o,205)==Action::None&&c.snapshot(0).error==Error::Route);
  }++groups;
  {
    Coordinator c;auto o=observation();route(c,o);o.binding.revision++;
    assert(c.poll(o,204)==Action::StopRoute);o.route=Route::Stopped;o.route_clean=true;
    assert(c.poll(o,205)==Action::None&&c.snapshot(0).phase==Phase::Blocked);
    Coordinator d;o=observation();route(d,o);
    assert(d.poll(o,202+Coordinator::RouteMs)==Action::StopRoute);
    assert(d.poll(o,202+Coordinator::RouteMs+5000)==Action::None&&d.snapshot(0).phase==Phase::Blocked);
  }++groups;
  {
    Coordinator c;auto o=observation();probe(c,o);o.probe_done=true;o.probe_reclaimed=false;
    assert(c.poll(o,206)==Action::None&&c.snapshot(0).phase==Phase::Probing);
    o.probe_reclaimed=true;o.probe_ok=true;assert(c.poll(o,207)==Action::None);
    assert(c.snapshot(0).phase==Phase::Recovering);
    o.free=400000;assert(c.poll(o,208)==Action::None&&c.blocksAutomaticWork());
    o.free=500000;assert(c.poll(o,209)==Action::None&&!c.blocksAutomaticWork()&&c.snapshot(209).cloud_verified);
  }++groups;
  {
    Coordinator c;auto o=observation();probe(c,o);o.probe_done=o.probe_reclaimed=true;o.probe_ok=false;
    c.poll(o,206);c.poll(o,207);assert(c.snapshot(0).phase==Phase::Blocked&&c.snapshot(0).error==Error::Probe);
    Coordinator d;o=observation();probe(d,o);assert(d.poll(o,205+Coordinator::ProbeMs)==Action::CancelProbe);
    assert(d.snapshot(0).phase==Phase::Blocked&&d.blocksAutomaticWork());
  }++groups;
  {
    Coordinator c;auto o=observation();probe(c,o);o.probe_done=o.probe_reclaimed=o.probe_ok=true;c.poll(o,206);
    o.largest=32*1024;assert(c.poll(o,207+Coordinator::RecoveryMs)==Action::None);
    assert(c.snapshot(0).error==Error::Memory&&c.blocksAutomaticWork());
    Coordinator d;o=observation();probe(d,o);o.binding.revision++;
    assert(d.poll(o,206)==Action::CancelProbe&&d.blocksAutomaticWork());
  }++groups;
  std::cout<<groups<<" public-WiFi policy groups passed; synthetic observations only\n";
}
