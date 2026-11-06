// Actual owner/coordinator/wire; synthetic SDK/USB/worker/route/probe boundaries.
#include "public_route_runtime_fixture.h"
#include <cassert>
int main(int argc,char** argv){
 assert(argc==3);const std::string scenario=argv[2];fixture(argv[1]);
 const auto nvs=testNvs;const auto writes=nvsWrites,starts=workerStarts;
 auto prepared=beginPortal();assert(prepared["ok"]==true);const auto grant=prepared["grant"].as<std::string>();
 auto ack=application("{\"op\":\"portal_handoff\",\"profile\":1,\"revision\":\"2\",\"grant\":\""+grant+"\"}");
 assert(ack["ok"]==true&&ack["phase"]=="disconnect_to_continue");
 publicConnected=false;testAuthorized=false;waitRoute();
 assert(routeState.phase==pro::owned_guest::Phase::Active&&!bleRunning&&maintenance&&probeStarts==0);
 routeState.phase=pro::owned_guest::Phase::Stopped;routeState.station_preserved=true;routeState.resources_retained=false;
 const bool healthy=scenario=="expired"||scenario=="normal";
 if(scenario=="ap-proof")routeState.error=pro::owned_guest::Error::ApProof;
 else if(scenario=="socket")routeState.error=pro::owned_guest::Error::Socket;
 else if(scenario=="expired")routeState.error=pro::owned_guest::Error::Expired;
 else if(scenario=="retained")routeState.resources_retained=true;
 else assert(scenario=="normal");
 tick();
 if(!healthy){
  const auto uart=command("PUBLIC_WIFI_STATUS");
  if(uart["public_wifi_phase"]!="blocked")std::cerr<<"Cleaned AP fault incorrectly accepted instead of blocked\n";
  assert(uart["public_wifi_phase"]=="blocked"&&uart["portal_error"]==unsigned(pro::public_wifi::Error::Route));
  for(unsigned i=0;i<4;++i)tick(10000);
  assert(probeStarts==0&&workerStarts==starts&&maintenance&&!bleRunning);
 }else{
  tick();assert(probeStarts==1&&probeState.phase==pro::public_wifi::ProbePhase::Running&&maintenance);
  probeState={pro::public_wifi::ProbePhase::Drained,true,207};tick();tick();
  assert(!maintenance&&workerStarts==starts+1&&probeReaps==1);
 }
 assert(testNvs==nvs&&nvsWrites==writes&&usbInitializations==1&&cloudUploadTests==0);
 std::cout<<"PASS actual runtime route acceptance "<<scenario<<"; no false probe/backup resume, NVS/USB unchanged\n";
}
