#pragma once
#ifndef CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
#define CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE 0
#endif
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE != 0 && CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE != 1
#error CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE must be zero or one
#endif
#if CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
#if !CSP_ENABLE_BACKUP_SETUP || !CSP_ENABLE_OWNED_GUEST_ROUTE
#error Public Wi-Fi assistance requires the combined backup and owned route targets
#endif
#include <cstdint>
namespace pro::public_wifi {
enum class Phase:uint8_t {Idle,Prepared,AwaitDisconnect,ClosingBle,WaitingSta,Routing,
  StoppingRoute,WaitingProbe,Probing,Recovering,Complete,Blocked};
enum class Error:uint8_t {None,Denied,Busy,Replay,Stale,Expired,Cancelled,Worker,
  Ble,Route,Probe,Memory,Timeout};
enum class Action:uint8_t {None,CloseBle,StartRoute,StopRoute,StartProbe,CancelProbe};
enum class Route:uint8_t {Stopped,Starting,Active,Stopping,Blocked};
struct Authority {
  uint32_t session=0,window=0;uint64_t generation=0;
  bool private_owner=false,physical_window=false,terminal=false;
};
struct Binding {uint32_t profile=0;uint64_t revision=0;bool valid=false;};
struct Observation {
  Binding binding;
  uint32_t window=0,free=0,largest=0;
  bool ble_active=false,connected=false,auth_matches=false,terminal=false,result_returned=false;
  bool worker_reclaimed=false,ble_clean=false,ble_failed=false,sta_valid=false;
  Route route=Route::Stopped;bool route_clean=false,probe_done=false,probe_reclaimed=false,probe_ok=false;
};
struct Snapshot {
  Phase phase=Phase::Idle;Error error=Error::None;
  uint64_t grant=0;uint32_t profile=0,remaining_ms=0;uint64_t revision=0;
  bool cloud_verified=false;
};
class Coordinator {
 public:
  static constexpr uint32_t PrepareMs=30000,HandoffMs=10000,BleMs=45000,StaMs=30000,
    RouteMs=300000,ProbeMs=45000,RecoveryMs=10000;
  Error begin(const Authority&,const Binding&,uint32_t request,uint32_t now);
  Error handoff(const Authority&,const Binding&,uint64_t grant,uint32_t request,uint32_t now);
  Error cancel(const Authority&,uint64_t grant,uint32_t request,uint32_t now);
  Action poll(const Observation&,uint32_t now);
  Snapshot snapshot(uint32_t now)const;
  bool blocksAutomaticWork()const{return phase_!=Phase::Idle&&phase_!=Phase::Complete;}
  bool prepared()const{return phase_==Phase::Prepared||phase_==Phase::AwaitDisconnect;}
 private:
  Authority owner_;Binding binding_;
  Phase phase_=Phase::Idle;Error error_=Error::None;
  uint64_t grant_=0,next_=1;uint32_t last_request_=0,at_=0,before_free_=0,before_largest_=0;
  bool result_ok_=false,route_seen_=false;
  static bool authorized(const Authority&);
  bool same(const Authority&)const;
  bool bound(const Binding&)const;
  void enter(Phase,uint32_t);
  void block(Error,uint32_t);
};
const char* phaseName(Phase);
} // namespace pro::public_wifi
#endif
