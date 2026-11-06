// Reuse the established synthetic NVS/USB/BLE/worker fixtures; the actual owner
// runtime, wire parser and public coordinator are linked unchanged.
#define ble_management_status legacy_fixture_ble_status
#include "runtime_fixture_prefix.h"
#undef ble_management_status
#include "public_wifi_probe.h"
#include "public_wifi_assistance.h"
#include "owned_guest_route.h"

bool publicConnected=false,terminalFrame=false;
unsigned routeStarts=0,routeStops=0,probeStarts=0,probeReaps=0;
bool badRoute=false,routeCleanupFault=false,probeCancelled=false,probeStartFail=false;
uint32_t routeAt=0;
pro::owned_guest::Status routeState;
pro::public_wifi::ProbeSnapshot probeState;
BleManagementStatus ble_management_status(){BleManagementStatus s;s.active=bleRunning;s.connected=bleRunning&&publicConnected;
  s.authenticated=bleRunning&&testAuthorized;s.window_generation=1;s.terminal_denial=terminalFrame;s.teardown_failed=!simulatedStopOk;return s;}
namespace pro::owned_guest {
bool start(const Request& request){
  assert(!bleRunning&&maintenance&&managed.phase==BackupWorkerPhase::Stopped);
  assert(request.ap_password=="admin123"&&request.ap_name.substr(0,10)=="CSP-Guest-"&&request.window_ms==300000);
  events.emplace_back("route-start");++routeStarts;routeAt=testNow;
  routeState={};routeState.station_preserved=true;
  if(badRoute)return false;
  routeState.phase=Phase::Starting;routeState.resources_retained=true;return true;
}
void poll(uint32_t now){
  if(routeState.phase==Phase::Starting)routeState.phase=Phase::Active;
  else if(routeState.phase==Phase::Active&&uint32_t(now-routeAt)>=300000){routeState.phase=Phase::Stopping;events.emplace_back("route-stop");++routeStops;}
  else if(routeState.phase==Phase::Stopping){
    routeState.phase=routeCleanupFault?Phase::Blocked:Phase::Stopped;routeState.resources_retained=routeCleanupFault;
  }
}
bool stop(){
  if(routeState.phase==Phase::Stopped)return true;
  if(routeState.phase!=Phase::Blocked){routeState.phase=Phase::Stopping;events.emplace_back("route-stop");++routeStops;}
  return false;
}
Status status(){return routeState;}
}
namespace pro::public_wifi {
bool probeStart(){
  assert(!bleRunning&&maintenance&&managed.phase==BackupWorkerPhase::Stopped&&routeState.phase==pro::owned_guest::Phase::Stopped);
  events.emplace_back("probe-start");++probeStarts;probeState={probeStartFail?ProbePhase::Failed:ProbePhase::Running,false,0};return !probeStartFail;
}
void probeCancel(){probeCancelled=true;}
ProbeSnapshot probeSnapshot(){return probeState;}
bool probeReap(){
  if(probeState.phase==ProbePhase::Drained||probeState.phase==ProbePhase::Failed){
    events.emplace_back("probe-reap");++probeReaps;probeState.phase=ProbePhase::Stopped;return true;
  }return probeState.phase==ProbePhase::Stopped;
}
bool readUplink(pro::owned_guest::Uplink& out){out={0x0a000002,0xffffff00,0x0a000001,0x08080808};return true;}
}
void fixture(const char* path){
  std::ifstream source(path,std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(source),{}};assert(bytes.size()==432);
  testNvs["csp-owner/owner"]=bytes;
  const std::string config="{\"schema\":1,\"mode\":\"upload_only\",\"private_fixture\":\"unchanged\"}";
  testNvs["csp/config"]={config.begin(),config.end()};
  pro::wifi_setup::StoredNetworks saved;saved.revision=2;saved.count=1;saved.selected=1;saved.next_id=2;
  auto& profile=saved.profiles[0];profile.id=1;std::strcpy(profile.ssid.data(),"Synthetic AP");std::strcpy(profile.secret.data(),"synthetic-wifi-password");
  WifiNvsStore nvs;pro::wifi_setup::ProfileStore fixture(nvs);
  assert(fixture.publish(saved,1,[](void*){return true;},nullptr)==pro::wifi_setup::Published::Committed);
  setup();tick();assert(workerStarts==1&&!maintenance);
  enterBle();testAuthorized=true;publicConnected=true;
}
JsonDocument beginPortal(){return application("{\"op\":\"portal_begin\",\"profile\":1,\"revision\":\"2\"}");}
JsonDocument statusPortal(){return application("{\"op\":\"portal_status\"}");}
void waitRoute(){for(unsigned i=0;i<20&&!routeStarts;++i)tick(501);assert(routeStarts==1);tick();}
int main(int argc,char** argv){
  assert(argc==3);const std::string scenario=argv[2];fixture(argv[1]);
#if CSP_PUBLIC_CLOCK_TEST
  advancePublicClock=true;
#endif
  const auto before=testNvs;const auto writes=nvsWrites;const auto initialStarts=workerStarts,usbBefore=usbPolls;
  if(scenario=="not-selected"){
    auto bad=application("{\"op\":\"portal_begin\",\"profile\":2,\"revision\":\"2\"}");
    assert(bad["ok"]==false&&routeStarts==0);return 0;
  }
  auto prepared=beginPortal();assert(prepared["ok"]==true&&prepared["phase"]=="prepared");
  assert(prepared["ap_password"]=="admin123"&&prepared["window_seconds"]==300&&prepared["handoff_ms"]==30000);
  const auto grant=prepared["grant"].as<std::string>();assert(!grant.empty());
  auto status=statusPortal();
  if(status["phase"]!="prepared")std::cerr<<"Clock regression: after begin phase="<<status["phase"].as<std::string>()<<" error="<<status["portal_error"].as<int>()<<'\n';
  assert(status["phase"]=="prepared"&&status["attempt_dav_read_verified"]==false);
  auto forbidden=application("{\"op\":\"scan\"}");assert(forbidden["ok"]==false&&routeStarts==0);
  if(scenario=="cancel"){
    auto cancelled=application("{\"op\":\"portal_cancel\",\"grant\":\""+grant+"\"}");
    assert(cancelled["ok"]==true&&cancelled["phase"]=="complete");assert(routeStarts==0&&testNvs==before&&nvsWrites==writes);return 0;
  }
  if(scenario=="prepare-expiry"){
    tick(30001);assert(statusPortal()["phase"]=="complete"&&routeStarts==0);return 0;
  }
  auto ack=application("{\"op\":\"portal_handoff\",\"profile\":1,\"revision\":\"2\",\"grant\":\""+grant+"\"}");
  assert(ack["ok"]==true&&ack["phase"]=="disconnect_to_continue"&&routeStarts==0&&bleRunning);
  tick();assert(routeStarts==0&&bleRunning); // A published/read ACK alone cannot tear down BLE.
  if(scenario=="terminal"){
    terminalFrame=true;tick();assert(statusPortal()["phase"]=="complete"&&routeStarts==0&&maintenance);return 0;
  }
  publicConnected=false;testAuthorized=false;
  if(scenario=="route-start-fail")badRoute=true;
  waitRoute();
  assert(!bleRunning&&maintenance&&workerStarts==initialStarts&&radioConnections>=1);
  if(scenario=="route-start-fail"){
    tick();assert(probeStarts==0&&maintenance);return 0;
  }
  const auto denied=command("BENCH_WINDOW");assert(denied["bench_window"]==false);
  const auto uart=command("PUBLIC_WIFI_STATUS");
  assert(uart["public_wifi_phase"]=="venue_sign_in"&&uart["grant"].isNull()&&uart["ap_password"].isNull()&&uart["ap_ssid"].isNull());
  const auto radioBefore=radioConnections;
  if(scenario=="route-cleanup-fail")routeCleanupFault=true;
  tick(300001);tick();tick();
  if(scenario=="route-cleanup-fail"){
    assert(maintenance&&probeStarts==0&&workerStarts==initialStarts);assert(command("PUBLIC_WIFI_STATUS")["public_wifi_phase"]=="blocked");return 0;
  }
  assert(probeStarts==1&&probeState.phase==pro::public_wifi::ProbePhase::Running&&maintenance);
  assert(radioConnections==radioBefore&&workerStarts==initialStarts); // No competing reconnect/backup work.
  if(scenario=="probe-timeout"){
    tick(45001);assert(probeCancelled&&maintenance&&workerStarts==initialStarts);
    probeState={pro::public_wifi::ProbePhase::Drained,false,-2010};tick();assert(maintenance);return 0;
  }
  const bool ok=scenario!="probe-fail";
  probeState={pro::public_wifi::ProbePhase::Drained,ok,ok?207:401};tick();
  assert(probeReaps==1&&maintenance&&workerStarts==initialStarts);
  if(scenario=="memory-low")runtimeFreeHeap=100000;
  tick();
  if(!ok||scenario=="memory-low"){
    tick(10001);assert(maintenance&&workerStarts==initialStarts);return 0;
  }
  assert(!maintenance&&workerStarts==initialStarts+1&&testNvs==before&&nvsWrites==writes);
  const auto route=std::find(events.begin(),events.end(),"route-start"),probe=std::find(events.begin(),events.end(),"probe-start");
  const auto reaped=std::find(events.begin(),events.end(),"probe-reap");
  assert(route<probe&&probe<reaped&&reaped<std::find(reaped,events.end(),"backup-start"));
  assert(usbInitializations==1&&usbPolls>usbBefore&&cloudUploadTests==0);
  std::cout<<"PASS actual owner runtime normal-disconnect handoff, exclusive route/probe, reaped-memory resume; NVS unchanged\n";
}
