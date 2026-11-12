// Actual product owner loop, reused real model/NVS source and controlled I/O.
#define CSP_GUEST_RUNTIME_TEST 1
#include "../backup_integration/runtime_handoff.test.cpp"
#include "guest_diagnostic.h"
#include "local_dashboard.h"
#include <esp_wifi.h>

bool diagnosticBusy=false,lastStartAdmission=false,dashboardAllowed=false;
bool dashboardStopOk=true;
unsigned diagnosticStarts=0,diagnosticCancels=0,diagnosticPolls=0,dashboardPolls=0,dashboardStops=0;
namespace pro::guest_diag {
bool start(bool admitted){++diagnosticStarts;lastStartAdmission=admitted;if(admitted)diagnosticBusy=true;USBSerial.println(admitted?"started":"rejected");return admitted;}
void cancel(){++diagnosticCancels;if(diagnosticBusy){diagnosticBusy=false;wifi_setup_radio_disconnect();}USBSerial.println("cancelled");}
void poll(){++diagnosticPolls;}
bool busy(){return diagnosticBusy;}
const char* screen(){return "Synthetic guest diagnostic";}
void report(){USBSerial.println("synthetic status");}
}
namespace pro::local_dashboard {
void poll(uint32_t,bool allowed,uint32_t,bool,const Snapshot&,uint32_t){++dashboardPolls;dashboardAllowed=allowed;}
bool stop(){++dashboardStops;return dashboardStopOk;}
bool listening(){return false;}
bool faulted(){return false;}
}
BackupPolicy backup_policy(){return {};}
bool backup_window_open(){return true;}
uint32_t storage_quiet_remaining(uint32_t){return 0;}

void serialText(const char* commandText){
  testNow+=300;USBSerial.send(commandText);
  while(USBSerial.available())loop();
}
int main(int argc,char** argv){
  assert(argc==3);const std::string scenario=argv[2];
  std::ifstream source(argv[1],std::ios::binary);
  std::vector<uint8_t> owner{std::istreambuf_iterator<char>(source),{}};assert(owner.size()==432);
  if(scenario!="owner-missing")testNvs["csp-owner/owner"]=owner;
  if(scenario=="invalid-config")configValid=false;
  pro::wifi_setup::StoredNetworks saved;saved.revision=2;saved.count=1;saved.selected=1;saved.next_id=2;
  auto& profile=saved.profiles[0];profile.id=1;std::strcpy(profile.ssid.data(),"Synthetic AP");std::strcpy(profile.secret.data(),"synthetic-wifi-password");
  WifiNvsStore nvs;pro::wifi_setup::ProfileStore fixture(nvs);
  assert(fixture.publish(saved,1,[](void*){return true;},nullptr)==pro::wifi_setup::Published::Committed);
  const auto before=testNvs;const auto writes=nvsWrites;
  setup();tick();assert(workerStarts==0&&maintenance);
  for(unsigned i=0;i<30;++i)tick(60000);
  assert(workerStarts==0&&managed.phase==BackupWorkerPhase::Stopped&&maintenance);
  assert(command("CHECK")["accepted"]==false);
  assert(command("POWER_SAVE_SET 0")["accepted"]==false);
  assert(power_save_fixture::psSetCalls==0&&power_save_fixture::psGetCalls==0);
  const auto polls=usbPolls;
  if(scenario=="owner-missing"){
    serialText("GUEST_TEST_START");assert(!lastStartAdmission&&!diagnosticBusy);
  }else if(scenario=="dashboard-stop-failure"){
    dashboardStopOk=false;serialText("GUEST_TEST_START");assert(!lastStartAdmission&&!diagnosticBusy);
  }else if(scenario=="ble-busy"){
    auto opened=command("BENCH_WINDOW");assert(opened["bench_window"]==true);tick();assert(bleRunning);
    serialText("GUEST_TEST_START");assert(!lastStartAdmission&&!diagnosticBusy);
    const auto priorState=radio.state;serialText("GUEST_TEST_CANCEL");
    assert(bleRunning&&radio.state==priorState);
  }else{
    serialText("GUEST_TEST_START");assert(lastStartAdmission&&diagnosticBusy&&dashboardStops>0);
    assert(!dashboardAllowed);const auto connects=radioConnections;
    radio.state=WifiSetupRadioState::Failed;
    for(unsigned i=0;i<20;++i)tick(1000);
    assert(radioConnections==connects&&!dashboardAllowed&&!bleRunning);
    testOwnerDown=true;tick();tick(4000);testOwnerDown=false;tick();assert(bleBegins==0);
    if(scenario=="bench-busy"){
      auto opened=command("BENCH_WINDOW");assert(opened["bench_window"]==false);
    }else if(scenario=="cancel-and-resume"){
      serialText("GUEST_TEST_CANCEL");assert(!diagnosticBusy&&diagnosticCancels==1);
      for(unsigned i=0;i<15;++i)tick(1000);
      assert(radioConnections>connects&&dashboardAllowed);
    }else if(scenario=="provisioning-after-cancel"){
      serialText("GUEST_TEST_CANCEL");assert(!diagnosticBusy);
      auto opened=command("BENCH_WINDOW");assert(opened["bench_window"]==true);tick();assert(bleRunning);
      testAuthorized=true;auto status=application("{\"op\":\"status\"}");assert(status["ok"]==true);
      auto begin=application("{\"op\":\"begin\",\"revision\":\"2\"}");assert(begin["ok"]==true);
      const auto priorState=radio.state;serialText("GUEST_TEST_CANCEL");
      assert(bleRunning&&radio.state==priorState);
    }else assert(scenario=="normal"||scenario=="invalid-config");
  }
  assert(workerStarts==0&&cloudUploadTests==0&&maintenance&&managed.phase==BackupWorkerPhase::Stopped);
  assert(usbInitializations==1&&usbPolls>polls&&diagnosticPolls>0&&dashboardPolls>0);
  assert(testNvs==before&&nvsWrites==writes&&testHandles.empty());
  for(const auto& line:USBSerial.lines)assert(line.find("synthetic-wifi-password")==std::string::npos);
  std::cout<<"PASS guest runtime "<<scenario<<"; actual owner loop with simulated I/O\n";
}
