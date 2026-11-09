// Actual production owner loop and NVS/model code, with synthetic radio/USB,
// heap/worker and dashboard socket boundaries. Not a physical socket/USB test.
#include <cstddef>
#include "local_dashboard.h"
#define ble_management_begin baselineBleBegin
// The runner extracts the unchanged fixture/helper prefix before its original
// main. This avoids duplicating hardware fixtures or suppressing diagnostics for
// an otherwise-unused renamed main's implicit return semantics.
#include "../../artifacts/local-dashboard-runtime/runtime-fixture.inc"
#undef ble_management_begin

namespace {
bool dashboardOpen=false,dashboardFault=false;
bool requireClosedForHeap=false;
unsigned dashboardStops=0,dashboardPolls=0,closedHeapSamples=0;
size_t dashboardHeapCost=400000;
pro::local_dashboard::Snapshot lastDashboard;
}

size_t dashboardRecordedHeap(){
  if(requireClosedForHeap){assert(!dashboardOpen);++closedHeapSamples;}
  return runtimeFreeHeap-(dashboardOpen?dashboardHeapCost:0);
}
bool ble_management_begin(){
  // If before_free had been captured before stop, its synthetic value would
  // be below BLE admission. The actual successful begin proves the ordering.
  assert(!dashboardOpen&&!dashboardFault&&closedHeapSamples>0);
  events.emplace_back("dashboard-proved-closed-before-ble");
  return baselineBleBegin();
}
BackupPolicy backup_policy(){return {};}
bool backup_window_open(){return true;}
uint32_t storage_quiet_remaining(uint32_t){return 0;}

namespace pro::local_dashboard {
bool stop(){
  ++dashboardStops;events.emplace_back("dashboard-stop");
  if(dashboardFault)return false;
  dashboardOpen=false;return true;
}
bool listening(){return dashboardOpen&&!dashboardFault;}
bool faulted(){return dashboardFault;}
void poll(uint32_t,bool allowed,uint32_t address,bool memory_ok,const Snapshot& snapshot,uint32_t){
  ++dashboardPolls;lastDashboard=snapshot;
  if(dashboardFault)return;
  if(!allowed||!address||!memory_ok){stop();return;}
  dashboardOpen=true;
}
}

int main(int argc,char** argv){
  assert(argc==3);const std::string scenario=argv[2];
  std::ifstream source(argv[1],std::ios::binary);
  std::vector<uint8_t> owner{std::istreambuf_iterator<char>(source),{}};
  assert(owner.size()==432);testNvs["csp-owner/owner"]=owner;
  const std::string cloudBlob="{\"schema\":1,\"mode\":\"upload_only\",\"private_fixture\":\"preserve\"}";
  testNvs["csp/config"]={cloudBlob.begin(),cloudBlob.end()};
  pro::wifi_setup::StoredNetworks saved;saved.revision=2;saved.count=1;
  saved.selected=1;saved.next_id=2;auto& profile=saved.profiles[0];profile.id=1;
  std::strcpy(profile.ssid.data(),"Synthetic private SSID");
  std::strcpy(profile.secret.data(),"synthetic-wifi-password");
  WifiNvsStore nvs;pro::wifi_setup::ProfileStore fixture(nvs);
  assert(fixture.publish(saved,1,[](void*){return true;},nullptr)==pro::wifi_setup::Published::Committed);
  if(scenario=="config-invalid")configValid=false;
  const auto persisted=testNvs;const auto writes=nvsWrites;
  setup();tick();assert(usbInitializations==1&&dashboardOpen);
  const auto openedHandles=nextHandle,reads=configReads,polls=usbPolls;
  if(scenario=="normal-close"){
    openWindow();assert(!dashboardOpen&&maintenance&&bleBegins==0);
    // Require every following heap observation to see a closed listener,
    // including the before_free sample and the canStart admission check.
    requireClosedForHeap=true;drained();tick();
    assert(bleBegins==1&&bleRunning&&dashboardStops>=2&&closedHeapSamples>0);
    const auto statusStart=USBSerial.lines.size();command("STATUS");
    bool baselineObserved=false;
    for(size_t i=statusStart;i<USBSerial.lines.size();++i){
      JsonDocument report;if(deserializeJson(report,USBSerial.lines[i]))continue;
      if(report["ble_before_free"].is<unsigned>()){
        assert(report["ble_before_free"].as<size_t>()==runtimeFreeHeap);baselineObserved=true;
      }
    }
    assert(baselineObserved);
  }else if(scenario=="uncertain-close"){
    // Plenty of heap ensures denial comes from the actual stop-failure gate,
    // not an accidental low-memory or unreaped-worker rejection.
    dashboardHeapCost=0;dashboardFault=true;openWindow();drained();
    for(unsigned i=0;i<100;++i)tick(20);
    assert(bleBegins==0&&!bleRunning&&dashboardOpen&&maintenance);
    assert(managed.phase==BackupWorkerPhase::Stopped&&runtimeFreeHeap>=500000);
  }else if(scenario=="snapshot-only"||scenario=="config-invalid"){
    observedBackup.hasScan=true;observedBackup.scanComplete=true;observedBackup.verified=4;
    std::strcpy(observedBackup.currentFile,"PRIVATE-SYNTHETIC-NAME.pdf");
    std::strcpy(observedBackup.message,"synthetic-cloud-password must never leave status");
    for(unsigned i=0;i<200;++i)tick(10);
    assert(dashboardPolls>=200&&dashboardOpen&&bleBegins==0);
    assert(lastDashboard.verified==4&&lastDashboard.configured==configValid);
    char body[pro::local_dashboard::StatusLimit]{};
    assert(pro::local_dashboard::json(lastDashboard,body,sizeof body)>0);
    const std::string json=body;
    for(const auto* secret:{"PRIVATE-SYNTHETIC-NAME","synthetic-cloud-password","synthetic-wifi-password","Synthetic private SSID","verifier"})
      assert(json.find(secret)==std::string::npos);
    // No new NVS handle or cloud/config load is requested by repeated status
    // sampling, even with unusable cloud settings. The private store is exact.
    assert(nextHandle==openedHandles&&configReads==reads&&testHandles.empty());
    if(!configValid)assert(workerStarts==0&&maintenance&&!lastDashboard.configured);
  }else assert(false);
  assert(testNvs==persisted&&nvsWrites==writes&&usbInitializations==1&&usbPolls>polls);
  std::cout<<"PASS dashboard actual-runtime "<<scenario
           <<"; synthetic boundaries; no device/network/cloud\n";
}
