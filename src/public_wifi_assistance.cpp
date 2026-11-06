#include "public_wifi_assistance.h"
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
namespace pro::public_wifi {
namespace {
uint32_t age(uint32_t now,uint32_t at){
  // All coordinator deadlines are below half the millis range. A slightly
  // older owner sample can precede a command's new timestamp; that is age zero,
  // not ~49 days elapsed. Forward millis rollover still has a small delta.
  const auto delta=uint32_t(now-at);return delta<0x80000000u?delta:0;
}
bool elapsed(uint32_t now,uint32_t at,uint32_t duration){return age(now,at)>=duration;}
uint32_t deadline(Phase phase){
  switch(phase){case Phase::Prepared:return Coordinator::PrepareMs;case Phase::AwaitDisconnect:return Coordinator::HandoffMs;
    case Phase::ClosingBle:return Coordinator::BleMs;case Phase::WaitingSta:return Coordinator::StaMs;
    case Phase::Routing:return Coordinator::RouteMs;case Phase::StoppingRoute:return 5000;
    case Phase::WaitingProbe:case Phase::Recovering:return Coordinator::RecoveryMs;
    case Phase::Probing:return Coordinator::ProbeMs;default:return 0;}
}
}
bool Coordinator::authorized(const Authority& a){return a.session&&a.window&&a.generation&&a.private_owner&&a.physical_window&&!a.terminal;}
bool Coordinator::same(const Authority& a)const{return authorized(a)&&a.session==owner_.session&&a.generation==owner_.generation&&a.window==owner_.window;}
bool Coordinator::bound(const Binding& b)const{return b.valid&&b.profile==binding_.profile&&b.revision==binding_.revision;}
void Coordinator::enter(Phase p,uint32_t now){phase_=p;at_=now;}
void Coordinator::block(Error e,uint32_t now){error_=e;enter(Phase::Blocked,now);}
Error Coordinator::begin(const Authority& a,const Binding& b,uint32_t request,uint32_t now){
  if(!authorized(a))return Error::Denied;
  if(same(a)&&request<=last_request_)return Error::Replay;
  if(phase_!=Phase::Idle&&phase_!=Phase::Complete)return request<=last_request_&&same(a)?Error::Replay:Error::Busy;
  if(!b.valid||!b.profile||!b.revision||!request)return Error::Stale;
  if(next_==UINT64_MAX)return Error::Replay;
  owner_=a;binding_=b;last_request_=request;grant_=next_++;error_=Error::None;result_ok_=route_seen_=false;
  enter(Phase::Prepared,now);return Error::None;
}
Error Coordinator::handoff(const Authority& a,const Binding& b,uint64_t grant,uint32_t request,uint32_t now){
  if(!same(a))return Error::Denied;
  if(request<=last_request_)return Error::Replay;
  if(phase_!=Phase::Prepared||grant!=grant_||!bound(b))return Error::Stale;
  if(elapsed(now,at_,PrepareMs)){error_=Error::Expired;enter(Phase::Complete,now);return Error::Expired;}
  last_request_=request;enter(Phase::AwaitDisconnect,now);return Error::None;
}
Error Coordinator::cancel(const Authority& a,uint64_t grant,uint32_t request,uint32_t now){
  if(!same(a))return Error::Denied;
  if(request<=last_request_)return Error::Replay;
  if(!prepared()||grant!=grant_)return Error::Stale;
  last_request_=request;error_=Error::Cancelled;enter(Phase::Complete,now);return Error::None;
}
Action Coordinator::poll(const Observation& o,uint32_t now){
  if(phase_==Phase::Idle||phase_==Phase::Complete||phase_==Phase::Blocked)return Action::None;
  if(!bound(o.binding)){
    if(prepared()){error_=Error::Stale;enter(Phase::Complete,now);return Action::None;}
    if(phase_==Phase::Routing){error_=Error::Stale;enter(Phase::StoppingRoute,now);return Action::StopRoute;}
    const bool probing=phase_==Phase::Probing;block(Error::Stale,now);return probing?Action::CancelProbe:Action::None;
  }
  if(prepared()){
    if(!o.ble_active||o.window!=owner_.window||o.terminal||(o.connected&&!o.auth_matches)){
      error_=Error::Denied;enter(Phase::Complete,now);return Action::None;
    }
    if(elapsed(now,at_,deadline(phase_))){error_=Error::Expired;enter(Phase::Complete,now);return Action::None;}
    if(phase_==Phase::Prepared&&!o.connected){error_=Error::Cancelled;enter(Phase::Complete,now);return Action::None;}
    if(phase_==Phase::AwaitDisconnect&&!o.connected){
      if(!o.result_returned){error_=Error::Cancelled;enter(Phase::Complete,now);return Action::None;}
      if(!o.worker_reclaimed){block(Error::Worker,now);return Action::None;}
      enter(Phase::ClosingBle,now);return Action::CloseBle;
    }
    return Action::None;
  }
  if(phase_==Phase::ClosingBle){
    if(o.ble_failed){block(Error::Ble,now);return Action::None;}
    if(o.ble_clean&&!o.ble_active&&o.worker_reclaimed){enter(Phase::WaitingSta,now);return Action::None;}
  }else if(phase_==Phase::WaitingSta){
    if(o.ble_active||!o.worker_reclaimed){block(Error::Worker,now);return Action::None;}
    if(o.sta_valid&&o.free>=160*1024&&o.largest>=64*1024){
      before_free_=o.free;before_largest_=o.largest;route_seen_=false;enter(Phase::Routing,now);return Action::StartRoute;
    }
  }else if(phase_==Phase::Routing){
    if(o.ble_active||!o.worker_reclaimed){error_=Error::Worker;enter(Phase::StoppingRoute,now);return Action::StopRoute;}
    if(o.route==Route::Blocked){block(Error::Route,now);return Action::None;}
    if(o.route==Route::Active)route_seen_=true;
    if(o.route==Route::Stopping){enter(Phase::StoppingRoute,now);return Action::None;}
    if(o.route==Route::Stopped){
      if(!route_seen_||!o.route_clean){block(Error::Route,now);return Action::None;}
      enter(Phase::WaitingProbe,now);return Action::None;
    }
    if(elapsed(now,at_,RouteMs)){enter(Phase::StoppingRoute,now);return Action::StopRoute;}
    return Action::None;
  }else if(phase_==Phase::StoppingRoute){
    if(o.route==Route::Blocked){block(Error::Route,now);return Action::None;}
    if(o.route==Route::Stopped){
      if(!o.route_clean||error_!=Error::None){block(error_==Error::None?Error::Route:error_,now);return Action::None;}
      enter(Phase::WaitingProbe,now);return Action::None;
    }
  }else if(phase_==Phase::WaitingProbe){
    if(o.route!=Route::Stopped||!o.route_clean||o.ble_active||!o.worker_reclaimed){block(Error::Route,now);return Action::None;}
    if(o.sta_valid&&o.free>=96*1024&&o.largest>=32*1024){enter(Phase::Probing,now);return Action::StartProbe;}
  }else if(phase_==Phase::Probing){
    if(o.ble_active||!o.worker_reclaimed||o.route!=Route::Stopped||!o.sta_valid){block(Error::Probe,now);return Action::CancelProbe;}
    if(o.probe_done&&o.probe_reclaimed){
      result_ok_=o.probe_ok;enter(Phase::Recovering,now);return Action::None;
    }
  }else if(phase_==Phase::Recovering){
    const bool restored=o.probe_reclaimed&&o.route==Route::Stopped&&o.route_clean&&o.worker_reclaimed&&!o.ble_active&&o.sta_valid&&
      o.free>=160*1024&&o.largest>=64*1024&&uint64_t(o.free)+8192>=before_free_&&uint64_t(o.largest)+8192>=before_largest_;
    if(restored){if(result_ok_)enter(Phase::Complete,now);else block(Error::Probe,now);return Action::None;}
  }
  if(elapsed(now,at_,deadline(phase_))){
    const auto probing=phase_==Phase::Probing;block(phase_==Phase::Recovering?Error::Memory:Error::Timeout,now);
    return probing?Action::CancelProbe:Action::None;
  }
  return Action::None;
}
Snapshot Coordinator::snapshot(uint32_t now)const{
  const auto duration=deadline(phase_),used=age(now,at_);
  return {phase_,error_,grant_,binding_.profile,duration&&used<duration?duration-used:0,binding_.revision,
    phase_==Phase::Complete&&result_ok_};
}
const char* phaseName(Phase phase){
  switch(phase){case Phase::Idle:return "idle";case Phase::Prepared:return "prepared";case Phase::AwaitDisconnect:return "disconnect_to_continue";
    case Phase::ClosingBle:return "closing_bluetooth";case Phase::WaitingSta:return "waiting_wifi";case Phase::Routing:return "venue_sign_in";
    case Phase::StoppingRoute:return "closing_assistance";case Phase::WaitingProbe:return "waiting_cloud_check";case Phase::Probing:return "checking_cloud";
    case Phase::Recovering:return "checking_memory";case Phase::Complete:return "complete";default:return "blocked";}
}
} // namespace pro::public_wifi
#endif
