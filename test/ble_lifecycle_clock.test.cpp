#include "pro_ble_lifecycle.h"
#include <cassert>
#include <iostream>
using Gate=pro::BleLifecycle;
using Action=Gate::Action;
using State=Gate::State;
int main(){
 unsigned groups=0;
 {
  Gate gate;assert(gate.requestWindow(1000)==Action::Pause);
  assert(gate.poll(999,true,false,500000,250000)==Action::None);
  assert(gate.state()==State::Waiting&&gate.reason()==Gate::Reason::None);
  assert(gate.poll(1001,false,false,500000,250000)==Action::Start);assert(gate.started(true));
  assert(gate.poll(999,false,true,500000,250000)==Action::None);assert(gate.state()==State::Active);
 }++groups;
 {
  Gate gate;assert(gate.button(true,1000)==Action::None);assert(gate.button(true,999)==Action::None);
  assert(gate.state()==State::Idle);assert(gate.button(true,3999)==Action::None);
  assert(gate.button(true,4000)==Action::Pause);
 }++groups;
 {
  Gate gate;const uint32_t at=UINT32_MAX-1000;assert(gate.button(true,at)==Action::None);
  assert(gate.button(true,at+Gate::HoldMs-1)==Action::None);assert(gate.button(true,at+Gate::HoldMs)==Action::Pause);
  assert(gate.poll(at+Gate::HoldMs-1,true,false,500000,250000)==Action::None);
  assert(gate.poll(at+Gate::HoldMs+Gate::QuiesceMs-1,true,false,500000,250000)==Action::None);
  assert(gate.poll(at+Gate::HoldMs+Gate::QuiesceMs,true,false,500000,250000)==Action::Stop);
  assert(gate.reason()==Gate::Reason::QuiesceTimeout&&!gate.stopped(false,false)&&gate.state()==State::Blocked);
  assert(gate.requestWindow(100000)==Action::None&&gate.poll(100001,false,false,500000,250000)==Action::None);
 }++groups;
 {
  Gate gate;const uint32_t at=UINT32_MAX-3000;assert(gate.requestWindow(at)==Action::Pause);
  assert(gate.poll(at+1,false,false,500000,250000)==Action::Start);assert(gate.started(true));
  assert(gate.poll(at+Gate::WindowMs-1,false,true,500000,250000)==Action::None);
  assert(gate.poll(at+Gate::WindowMs,false,true,500000,250000)==Action::Stop);assert(gate.reason()==Gate::Reason::WindowExpired);
 }++groups;
 {
  Gate gate;assert(gate.requestWindow(1000)==Action::Pause);assert(gate.poll(1001,false,false,500000,250000)==Action::Start);assert(gate.started(true));
  assert(gate.poll(999,false,true,Gate::ActiveFree-1,250000)==Action::Stop);assert(gate.reason()==Gate::Reason::LowMemory);
  Gate closed;assert(closed.requestWindow(1000)==Action::Pause);assert(closed.poll(1001,false,false,500000,250000)==Action::Start);assert(closed.started(true));
  assert(closed.poll(999,false,false,500000,250000)==Action::Stop);assert(closed.reason()==Gate::Reason::TransportClosed);
 }++groups;
 std::cout<<groups<<" BLE clock groups passed; stale time ignored; physical hold, real deadlines, memory/transport denial preserved\n";
}
