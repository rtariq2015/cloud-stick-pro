#include "pro_ble_lifecycle.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

static unsigned checks=0,groups=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Gate=pro::BleLifecycle;
using Action=Gate::Action;
using State=Gate::State;
using Reason=Gate::Reason;
constexpr bool Guided=pro::setup_session::Guided;
static void start(Gate& gate,uint32_t at){
  CHECK(gate.requestWindow(at)==Action::Pause);
  CHECK(gate.paused());CHECK(!gate.activeWindow(at,true));
  CHECK(gate.poll(at+1,false,false,500000,250000)==Action::Start);
  CHECK(!gate.activeWindow(at+1,true));CHECK(gate.started(true));
  CHECK(gate.state()==State::Active);
}
static void deadlines(){
  CHECK(pro::setup_session::AdvertisingMs==120000);
  CHECK(pro::setup_session::HandshakeMs==30000);
  CHECK(pro::setup_session::AuthenticatedIdleMs==(Guided?600000u:60000u));
  Gate gate;start(gate,1000);
  CHECK(gate.activeWindow(120999));CHECK(gate.activeWindow(120999,true));
  CHECK(!gate.activeWindow(121000)); // No authority is inferred or cached.
  CHECK(gate.activeWindow(121000,true)==Guided);
  CHECK(!gate.activeWindow(121000,false));
  CHECK(gate.activeWindow(601001,true)==Guided);
  CHECK(!gate.activeWindow(601001,false)); // Fresh authority must be supplied each time.
  const auto action=gate.poll(601001,false,true,500000,250000);
  CHECK(action==(Guided?Action::None:Action::Stop));
  CHECK(gate.state()==(Guided?State::Active:State::Stopping));
  CHECK(gate.reason()==(Guided?Reason::None:Reason::WindowExpired));
  // The guided lifecycle must not invent its own management/auth timeout.
  // The transport's coherent management snapshot is the supplied authority.
  if(Guided){
    CHECK(gate.poll(3601000,false,true,500000,250000)==Action::None);
    CHECK(gate.activeWindow(3601000,true));CHECK(!gate.activeWindow(3601000));
    CHECK(gate.requestClose()==Action::Stop);
  }
  CHECK(!gate.activeWindow(3601000,true));CHECK(gate.stopped(true,true));
  CHECK(!gate.paused());++groups;
}
static void safety(){
  for(bool largest:{false,true}){
    Gate gate;start(gate,100);
    const auto free=largest?Gate::ActiveFree:Gate::ActiveFree-1;
    const auto block=largest?Gate::ActiveLargest-1:Gate::ActiveLargest;
    CHECK(gate.poll(200,false,true,free,block)==Action::Stop);
    CHECK(gate.reason()==Reason::LowMemory);CHECK(gate.paused());
    CHECK(!gate.activeWindow(200,true));CHECK(gate.stopped(true,true));
  }
  Gate closed;start(closed,100);
  CHECK(closed.poll(800000,false,false,500000,250000)==Action::Stop);
  CHECK(closed.reason()==Reason::TransportClosed);CHECK(closed.stopped(true,true));
  if(Guided){
    Gate late;start(late,100);
    CHECK(late.poll(800000,false,true,Gate::ActiveFree-1,Gate::ActiveLargest)==Action::Stop);
    CHECK(late.reason()==Reason::LowMemory);CHECK(late.stopped(true,true));
  }
  ++groups;
}
static void quiescence(){
  Gate gate;CHECK(gate.requestWindow(1000)==Action::Pause);
  CHECK(!gate.activeWindow(1000,true));
  CHECK(gate.poll(45999,true,false,500000,250000)==Action::None);
  CHECK(gate.poll(46000,true,false,500000,250000)==Action::Stop);
  CHECK(gate.reason()==Reason::QuiesceTimeout);CHECK(!gate.activeWindow(46000,true));
  CHECK(gate.stopped(true,true));
  Gate failed;CHECK(failed.requestWindow(0)==Action::Pause);
  CHECK(failed.poll(1,false,false,500000,250000)==Action::Start);CHECK(failed.started(false));
  CHECK(failed.reason()==Reason::StartFailed);CHECK(!failed.activeWindow(1,true));
  CHECK(!failed.stopped(false,true));CHECK(failed.state()==State::Blocked);
  CHECK(failed.requestWindow(100000)==Action::None);++groups;
}
static void stopProof(){
  for(unsigned failure=0;failure<2;++failure){
    Gate gate;start(gate,100);
    CHECK(gate.requestClose()==Action::Stop);
    CHECK(!gate.stopped(failure!=0,failure==0));
    CHECK(gate.reason()==(failure==0?Reason::StopFailed:Reason::MemoryNotRestored));
    CHECK(gate.state()==State::Blocked);CHECK(gate.paused());
    CHECK(!gate.activeWindow(800000,true));
    CHECK(gate.requestWindow(800000)==Action::None);
    CHECK(gate.poll(800001,false,true,500000,250000)==Action::None);
    CHECK(!gate.stopped(true,true)); // No implicit recovery from a blocked state.
  }
  CHECK(Gate::restored(200000,100000,200000-Gate::RestoreTolerance,100000-Gate::RestoreTolerance));
  CHECK(!Gate::restored(200000,100000,200000-Gate::RestoreTolerance-1,100000));
  CHECK(!Gate::restored(200000,100000,200000,100000-Gate::RestoreTolerance-1));
  CHECK(!Gate::restored(1000,1000,1000,1000));++groups;
}
static void clocks(){
  const auto at=std::numeric_limits<uint32_t>::max()-5000;
  Gate gate;start(gate,at);
  CHECK(gate.activeWindow(uint32_t(at+119999)));
  CHECK(!gate.activeWindow(uint32_t(at+120000)));
  CHECK(gate.activeWindow(uint32_t(at+120000),true)==Guided);
  CHECK(gate.poll(uint32_t(at+120000),false,true,500000,250000)==(Guided?Action::None:Action::Stop));
  if(Guided){CHECK(gate.activeWindow(uint32_t(at+600000),true));CHECK(!gate.activeWindow(uint32_t(at+600000)));CHECK(gate.requestClose()==Action::Stop);}
  CHECK(gate.stopped(true,true));
  Gate waiting;CHECK(waiting.requestWindow(at)==Action::Pause);
  CHECK(waiting.poll(uint32_t(at+44999),true,false,500000,250000)==Action::None);
  CHECK(waiting.poll(uint32_t(at+45000),true,false,500000,250000)==Action::Stop);
  CHECK(waiting.reason()==Reason::QuiesceTimeout);
  Gate stale;start(stale,1000);
  CHECK(stale.poll(999,false,true,500000,250000)==Action::None);
  CHECK(stale.state()==State::Active);++groups;
}
static void repeated(){
  for(uint32_t cycle=0;cycle<100;++cycle){
    Gate gate;const auto at=cycle*1000000u;start(gate,at);
    CHECK(gate.activeWindow(at+120001,true)==Guided);
    CHECK(!gate.activeWindow(at+120001,false));
    CHECK(gate.poll(at+120001,false,true,Gate::ActiveFree,Gate::ActiveLargest)==(Guided?Action::None:Action::Stop));
    if(Guided)CHECK(gate.requestClose()==Action::Stop);
    CHECK(gate.stopped(true,true));CHECK(gate.state()==State::Idle);
    CHECK(!gate.activeWindow(at+120002,true));
  }
  ++groups;
}
int main(){
  deadlines();safety();quiescence();stopProof();clocks();repeated();
  std::printf("%u lifecycle groups; %u checks; guided=%s; 100 repeated sessions\n",groups,checks,Guided?"true":"false");
}
