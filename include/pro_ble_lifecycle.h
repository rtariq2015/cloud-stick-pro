#pragma once
#include <cstdint>
#include "pro_setup_session.h"

namespace pro {
// Pure policy used by the Arduino adapter and desktop tests. No storage, radio,
// network or reset primitives exist here. Short deadlines tolerate stale samples
// and forward millis rollover; every deadline is below half the uint32 range.
class BleLifecycle {
 public:
  enum class State : uint8_t { Idle, Waiting, Starting, Active, Stopping, Blocked };
  enum class Action : uint8_t { None, Pause, Start, Stop };
  enum class Reason : uint8_t { None, QuiesceTimeout, StartFailed, WindowExpired,
                                LowMemory, TransportClosed, StopFailed, MemoryNotRestored };
  static constexpr uint32_t HoldMs=3000, QuiesceMs=45000, WindowMs=120000;
  static constexpr uint32_t StartFree=160*1024, StartLargest=64*1024;
  static constexpr uint32_t ActiveFree=32*1024, ActiveLargest=16*1024;
  static constexpr uint32_t RestoreTolerance=8*1024;

  Action button(bool down,uint32_t now) {
    if(!down){holding_=false;latched_=false;return Action::None;}
    if(!holding_){holding_=true;downAt_=now;}
    if(latched_||age(now,downAt_)<HoldMs)return Action::None;
    latched_=true;
    return requestWindow(now);
  }
  // Adapters decide admission. Only the explicit SD-absent bench target exposes
  // this transition over local USB; production still admits physical button only.
  Action requestWindow(uint32_t now) {
    if(state_!=State::Idle)return Action::None;
    openedAt_=now;reason_=Reason::None;state_=State::Waiting;return Action::Pause;
  }
  Action requestClose() {
    if(state_!=State::Active&&state_!=State::Waiting)return Action::None;
    reason_=Reason::TransportClosed;state_=State::Stopping;return Action::Stop;
  }
  Action poll(uint32_t now,bool backupActive,bool transportActive,
              uint32_t freeHeap,uint32_t largest) {
    if(state_==State::Waiting){
      if(age(now,openedAt_)>=QuiesceMs){reason_=Reason::QuiesceTimeout;state_=State::Stopping;return Action::Stop;}
      if(!backupActive){state_=State::Starting;return Action::Start;}
    }else if(state_==State::Active){
      if(!transportActive)reason_=Reason::TransportClosed;
      // The guided profile's management layer owns the coherent auth/activity
      // deadline. A second independently sampled absolute timer could kill a
      // valid authenticated transition just as that timer crossed 120 seconds.
      else if(!pro::setup_session::Guided&&age(now,openedAt_)>=WindowMs)reason_=Reason::WindowExpired;
      else if(freeHeap<ActiveFree||largest<ActiveLargest)reason_=Reason::LowMemory;
      else return Action::None;
      state_=State::Stopping;return Action::Stop;
    }
    return Action::None;
  }
  static bool canStart(uint32_t freeHeap,uint32_t largest){return freeHeap>=StartFree&&largest>=StartLargest;}
  bool started(bool ok) {
    if(state_!=State::Starting)return false;
    state_=ok?State::Active:State::Stopping;
    if(!ok)reason_=Reason::StartFailed;
    return true;
  }
  // Only confirmed shutdown and recovered memory authorize the adapter to resume.
  bool stopped(bool transportStopped,bool memoryRestored) {
    if(state_!=State::Stopping)return false;
    if(!transportStopped){state_=State::Blocked;reason_=Reason::StopFailed;return false;}
    if(!memoryRestored){state_=State::Blocked;reason_=Reason::MemoryNotRestored;return false;}
    state_=State::Idle;return true;
  }
  static bool restored(uint32_t beforeFree,uint32_t beforeLargest,
                       uint32_t afterFree,uint32_t afterLargest){
    return uint64_t(afterFree)+RestoreTolerance>=beforeFree&&
           uint64_t(afterLargest)+RestoreTolerance>=beforeLargest&&
           afterFree>=StartFree&&afterLargest>=StartLargest;
  }
  State state()const{return state_;}
  bool activeWindow(uint32_t now,bool authenticatedAuthority=false)const{
    return state_==State::Active&&((pro::setup_session::Guided&&authenticatedAuthority)||age(now,openedAt_)<WindowMs);
  }
  Reason reason()const{return reason_;}
  bool paused()const{return state_!=State::Idle;}
 private:
  static uint32_t age(uint32_t now,uint32_t at){
    const auto delta=uint32_t(now-at);return delta<0x80000000u?delta:0;
  }
  State state_=State::Idle;Reason reason_=Reason::None;
  uint32_t downAt_=0,openedAt_=0;bool holding_=false,latched_=false;
};
}
