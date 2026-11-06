// Real UART parser/owner runtime, synthetic NVS and controlled Wi-Fi SDK only.
#include "runtime_fixture_prefix.h"
#include <esp_wifi.h>
using namespace power_save_fixture;
#include "../../src/wifi_setup_runtime.cpp"

JsonDocument powerCommand(const std::string& text){
  // Exercise actual UART grammar without letting unrelated recovery loops change
  // the intentionally injected admission state before the next assertion.
  testNow+=300;const auto count=USBSerial.lines.size();USBSerial.send(text);
  while(USBSerial.available())serial();
  assert(USBSerial.lines.size()==count+1);
  JsonDocument result;assert(!deserializeJson(result,USBSerial.lines.back()));
  assert(result["accepted"].is<bool>());
#if CSP_USB_BENCH_AUTOMATION
  assert(result.size()==3&&result["mode"].is<int>()&&result["sdk_error"].is<int>());
#endif
  return result;
}
void denied(const std::string& text,int error){
  const auto sets=psSetCalls,gets=psGetCalls;
  const auto result=powerCommand(text);
  assert(result["accepted"]==false);
#if CSP_USB_BENCH_AUTOMATION
  assert(result["mode"]==-1&&result["sdk_error"]==error);
#else
  (void)error;
#endif
  assert(psSetCalls==sets&&psGetCalls==gets);
}
int main(int argc,char** argv){
  assert(argc==2);std::ifstream input(argv[1],std::ios::binary);
  std::vector<uint8_t> record{std::istreambuf_iterator<char>(input),{}};assert(record.size()==432);
  testNvs["csp-owner/owner"]=record;
  const std::string cloud="{\"schema\":1,\"private_fixture\":\"unchanged\"}";
  testNvs["csp/config"]={cloud.begin(),cloud.end()};
  pro::wifi_setup::StoredNetworks saved;saved.revision=2;saved.count=1;saved.selected=1;saved.next_id=2;
  saved.profiles[0].id=1;std::strcpy(saved.profiles[0].ssid.data(),"Synthetic AP");
  std::strcpy(saved.profiles[0].secret.data(),"synthetic-wifi-password");
  WifiNvsStore fixtureNvs;pro::wifi_setup::ProfileStore fixture(fixtureNvs);
  assert(fixture.publish(saved,1,[](void*){return true;},nullptr)==pro::wifi_setup::Published::Committed);
  const auto beforeNvs=testNvs;const auto beforeWrites=nvsWrites;
  setup();tick();assert(managed.phase==BackupWorkerPhase::Running&&!bleRunning);
  assert(psSetCalls==0&&psGetCalls==0); // Startup keeps the normal policy untouched.
  const auto beforeConnections=radioConnections,beforeReleases=radioReleases,beforeInit=usbInitializations;
  const auto beforeEvents=events;
#if CSP_USB_BENCH_AUTOMATION
  auto result=powerCommand("POWER_SAVE_STATUS");
  assert(result["accepted"]==true&&result["mode"]==1&&result["sdk_error"]==0);
  assert(psSetCalls==0&&psGetCalls==1);
  for(const int mode:{0,1,0,1}){
    psCalls.clear();result=powerCommand("POWER_SAVE_SET "+std::to_string(mode));
    assert(result["accepted"]==true&&result["mode"]==mode&&result["sdk_error"]==0);
    assert((psCalls==std::vector<int>{100+mode,200}));
  }
  observedPs=WIFI_PS_MAX_MODEM;result=powerCommand("POWER_SAVE_STATUS");
  assert(result["accepted"]==true&&result["mode"]==2); // Status reports the driver, never fabricates0/1.
  for(const auto* text:{"POWER_SAVE_SET","POWER_SAVE_SET ","POWER_SAVE_SET 2","POWER_SAVE_SET -1",
      "POWER_SAVE_SET 00","POWER_SAVE_SET 01","POWER_SAVE_SET 0 ","POWER_SAVE_SET  0",
      "POWER_SAVE_SET 1x","POWER_SAVE_STATUS ","POWER_SAVE_STATUS 0","POWER_SAVE_UNKNOWN"})
    denied(text,ESP_ERR_INVALID_ARG);
  const auto resetEligibility=[](){
    owner_state=WifiOwnerLoad::Ready;radio_ready=true;store_fault=false;backup_bridge_fault=false;
    closing=false;bleRunning=false;lifecycle=pro::BleLifecycle{};
    radio.state=WifiSetupRadioState::LocalConnected;managed.phase=BackupWorkerPhase::Running;
  };
  for(int gate=0;gate<8;++gate){
    resetEligibility();
    switch(gate){
      case 0:owner_state=WifiOwnerLoad::Missing;break;
      case 1:owner_state=WifiOwnerLoad::Fault;break;
      case 2:radio_ready=false;break;
      case 3:store_fault=true;break;
      case 4:backup_bridge_fault=true;break;
      case 5:closing=true;break;
      case 6:bleRunning=true;break;
      case 7:assert(lifecycle.requestWindow(testNow)==pro::BleLifecycle::Action::Pause);break;
    }
    denied("POWER_SAVE_STATUS",ESP_ERR_INVALID_STATE);denied("POWER_SAVE_SET 0",ESP_ERR_INVALID_STATE);
  }
  for(const auto state:{WifiSetupRadioState::Idle,WifiSetupRadioState::Scanning,WifiSetupRadioState::Cancelling,
      WifiSetupRadioState::Connecting,WifiSetupRadioState::Failed,WifiSetupRadioState::Fault}){
    resetEligibility();radio.state=state;denied("POWER_SAVE_SET 1",ESP_ERR_INVALID_STATE);
  }
  for(const auto phase:{BackupWorkerPhase::Stopped,BackupWorkerPhase::Starting,BackupWorkerPhase::Retiring,
      BackupWorkerPhase::Drained,BackupWorkerPhase::Failed}){
    resetEligibility();managed.phase=phase;denied("POWER_SAVE_SET 0",ESP_ERR_INVALID_STATE);
  }
  for(int phase=0;phase<4;++phase){
    resetEligibility();lifecycle.requestWindow(testNow);lifecycle.poll(testNow,false,false,500000,250000);
    if(phase>0)lifecycle.started(true);
    if(phase>1)lifecycle.requestClose();
    if(phase>2)lifecycle.stopped(false,false);
    denied("POWER_SAVE_STATUS",ESP_ERR_INVALID_STATE);
  }
  resetEligibility();psCalls.clear();psSetError=ESP_FAIL;
  result=powerCommand("POWER_SAVE_SET 0");
  assert(result["accepted"]==false&&result["mode"]==-1&&result["sdk_error"]==ESP_FAIL);
  assert((psCalls==std::vector<int>{100}));psSetError=ESP_OK;
  psGetError=ESP_ERR_NO_MEM;psCalls.clear();result=powerCommand("POWER_SAVE_SET 0");
  assert(result["accepted"]==false&&result["mode"]==-1&&result["sdk_error"]==ESP_ERR_NO_MEM);
  assert((psCalls==std::vector<int>{100,200}));result=powerCommand("POWER_SAVE_STATUS");
  assert(result["accepted"]==false&&result["mode"]==-1&&result["sdk_error"]==ESP_ERR_NO_MEM);
  psGetError=ESP_OK;psApply=false;observedPs=WIFI_PS_MIN_MODEM;
  result=powerCommand("POWER_SAVE_SET 0");
  assert(result["accepted"]==false&&result["mode"]==1&&result["sdk_error"]==ESP_ERR_INVALID_STATE);
  psApply=true;result=powerCommand("POWER_SAVE_SET 1");assert(result["accepted"]==true);
  std::cout<<"PASS actual UART grammar, admission, SDK failures/readback and unchanged persistence\n";
#else
  denied("POWER_SAVE_STATUS",0);denied("POWER_SAVE_SET 0",0);denied("POWER_SAVE_SET 1",0);
  std::cout<<"PASS bench-disabled actual UART rejects power controls without SDK calls\n";
#endif
  assert(testNvs==beforeNvs&&nvsWrites==beforeWrites);
  assert(radioConnections==beforeConnections&&radioReleases==beforeReleases&&usbInitializations==beforeInit);
  assert(events==beforeEvents); // No retirement, radio/USB restart, or maintenance operation.
  return 0;
}
