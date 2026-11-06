// The existing fixture/helper prefix is generated unchanged by run.py. The
// actual production owner runtime below remains the code under test.
#include "runtime-fixture-prefix.h"
#include "backup_wifi_power.h"
#include <esp_wifi.h>
using namespace power_save_fixture;
#include "../../src/wifi_setup_runtime.cpp"

unsigned baselineProofs=0;
wifi_ps_type_t expectedBeforeBle=WIFI_PS_MIN_MODEM;
size_t backupPowerRecordedHeap(){
  if(lifecycle.state()==pro::BleLifecycle::State::Starting){
    assert(!backup_power.owned()&&!backup_power.faulted());
    assert(observedPs==expectedBeforeBle);++baselineProofs;
    events.emplace_back("power-restored-before-heap-baseline");
  }
  return runtimeFreeHeap;
}
size_t position(const std::string& event,size_t first=0){
  for(size_t i=first;i<events.size();++i)if(events[i]==event)return i;
  assert(false);return 0;
}
void assertNormalOrder(size_t first,wifi_ps_type_t previous){
  const auto reaped=position("backup-reap",first);
  const auto restored=previous==WIFI_PS_NONE?reaped:position("ps-write-"+std::to_string(int(previous)),reaped);
  const auto proof=position("ps-read-"+std::to_string(int(previous)),restored);
  const auto heap=position("power-restored-before-heap-baseline",proof);
  const auto begin=position("ble-start",heap);
  assert(reaped<=restored&&restored<proof&&proof<heap&&heap<begin);
}
JsonDocument readPowerCommand(const std::string& text){
  testNow+=300; // Respect the actual UART reply-rate gate, as the base fixture does.
  const auto count=USBSerial.lines.size();USBSerial.send(text);
  while(USBSerial.available())serial();
  assert(USBSerial.lines.size()==count+1);
  JsonDocument result;assert(!deserializeJson(result,USBSerial.lines.back()));return result;
}
void assertRamPolicyStatus(){
  const auto gets=psGetCalls,sets=psSetCalls,handles=nextHandle,reads=configReads;
  const auto priorEvents=events;const auto nvs=testNvs;
  auto status=readPowerCommand("POWER_POLICY_STATUS");
  const std::set<std::string> wanted={"owned","fault","sdk_error","previous"};
  assert(status.size()==wanted.size()&&USBSerial.lines.back().size()<128);
  for(auto row:status.as<JsonObject>())assert(wanted.count(row.key().c_str())==1);
  assert(status["owned"].is<bool>()&&status["fault"].is<unsigned>()&&status["sdk_error"].is<int>()&&status["previous"].is<int>());
  assert(status["owned"]==backup_power.owned()&&status["fault"]==unsigned(backup_power.fault()));
  assert(status["fault"].as<unsigned>()<=unsigned(pro::BackupWifiPower::Fault::RestoreMismatch));
  assert(status["sdk_error"]==backup_power.error()&&status["previous"]==backup_power.previous());
  assert(status["previous"].as<int>()>=-1&&status["previous"].as<int>()<=2);
  assert(psGetCalls==gets&&psSetCalls==sets&&nextHandle==handles&&configReads==reads&&testNvs==nvs&&events==priorEvents);
}
int main(int argc,char** argv){
  assert(argc==3);const std::string scenario=argv[2];
  std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> owner{std::istreambuf_iterator<char>(input),{}};
  assert(owner.size()==432);testNvs["csp-owner/owner"]=owner;
  const std::string cloud="{\"schema\":1,\"mode\":\"upload_only\",\"private_fixture\":\"unchanged\"}";
  testNvs["csp/config"]={cloud.begin(),cloud.end()};
  StoredNetworks saved;saved.revision=2;saved.count=1;saved.selected=1;saved.next_id=2;
  saved.profiles[0].id=1;std::strcpy(saved.profiles[0].ssid.data(),"Synthetic AP");
  std::strcpy(saved.profiles[0].secret.data(),"synthetic-wifi-password");
  WifiNvsStore fixtureNvs;ProfileStore fixture(fixtureNvs);
  assert(fixture.publish(saved,1,[](void*){return true;},nullptr)==Published::Committed);
  const auto nvsBefore=testNvs;const auto writes=nvsWrites;
  if(scenario=="normal-max")observedPs=WIFI_PS_MAX_MODEM;
  if(scenario=="normal-none")observedPs=WIFI_PS_NONE;
  expectedBeforeBle=observedPs;
  if(scenario=="capture-read-fail")psGetError=ESP_FAIL;
  if(scenario=="awake-set-fail")psSetError=ESP_FAIL;
  if(scenario=="awake-read-fail")backup_power_fixture::failReadAt=2;
  if(scenario=="awake-mismatch")psApply=false;
  // 3 is representable by this enum's underlying value range but unsupported
  // by the reviewed SDK policy; avoid constructing an invalid C++ enum value.
  if(scenario=="invalid-mode")observedPs=static_cast<wifi_ps_type_t>(3);
  if(scenario=="worker-start-fail")startOk=false;
  setup();tick();assert(usbInitializations==1&&usbPolls>0);
  const bool acquisitionFailure=scenario=="capture-read-fail"||scenario=="awake-set-fail"||
    scenario=="awake-read-fail"||scenario=="awake-mismatch"||scenario=="invalid-mode";
  if(acquisitionFailure){
    assert(workerStarts==0&&maintenance&&backup_bridge_fault&&backup_power.faulted()&&!bleRunning);
    const auto gets=psGetCalls,sets=psSetCalls;openWindow();
    for(unsigned i=0;i<10;++i)tick(100);
    assert(bleBegins==0&&radioReleases==0&&psGetCalls==gets&&psSetCalls==sets);
  }else if(scenario=="worker-start-fail"){
    assert(workerStarts==1&&managed.phase==BackupWorkerPhase::Failed&&backup_power.owned()&&observedPs==WIFI_PS_NONE);
    tick();assert(managed.phase==BackupWorkerPhase::Stopped&&!backup_power.owned()&&observedPs==expectedBeforeBle);
    assert(workerStarts==1);startOk=true;tick(5001);
    assert(workerStarts==2&&managed.phase==BackupWorkerPhase::Running&&backup_power.owned()&&observedPs==WIFI_PS_NONE);
  }else{
    assert(workerStarts==1&&managed.phase==BackupWorkerPhase::Running&&backup_power.owned()&&observedPs==WIFI_PS_NONE);
    if(scenario=="manual-control"){
      const auto sets=psSetCalls,gets=psGetCalls;auto status=readPowerCommand("POWER_SAVE_STATUS");
      assert(status["accepted"]==true&&status["mode"]==0&&psGetCalls==gets+1);
      for(const auto* command:{"POWER_SAVE_SET 0","POWER_SAVE_SET 1"}){
        auto result=readPowerCommand(command);assert(result["accepted"]==false&&result["sdk_error"]==ESP_ERR_INVALID_STATE);
      }
      assert(psSetCalls==sets&&psGetCalls==gets+1&&backup_power.owned()&&!backup_power.faulted());
    }else{
      const auto beginning=events.size();openWindow();const auto gets=psGetCalls,sets=psSetCalls;
      // A request or even an unparked Drained state does not restore power.
      for(unsigned i=0;i<3;++i)tick(20);
      assert(backup_power.owned()&&psGetCalls==gets&&psSetCalls==sets&&bleBegins==0);
      drained(false);tick();assert(backup_power.owned()&&psGetCalls==gets&&psSetCalls==sets&&bleBegins==0);
      if(scenario=="stale-retirement")managed.retired_generation=managed.generation-1;
      if(scenario=="restore-set-fail")psSetError=ESP_FAIL;
      if(scenario=="restore-read-fail")psGetError=ESP_FAIL;
      if(scenario=="restore-mismatch")psApply=false;
      reapReady=true;tick();
      if(scenario=="stale-retirement"){
        assert(bleBegins==0&&backup_power.owned()&&psGetCalls==gets&&psSetCalls==sets);
      }else if(scenario=="restore-set-fail"||scenario=="restore-read-fail"||scenario=="restore-mismatch"){
        assert(backup_power.owned()&&backup_power.faulted()&&backup_bridge_fault&&maintenance);
        assert(bleBegins==0&&radioReleases==0&&managed.phase==BackupWorkerPhase::Stopped);
        const auto failedGets=psGetCalls,failedSets=psSetCalls;
        psGetError=psSetError=ESP_OK;psApply=true;
        for(unsigned i=0;i<10;++i)tick(100);
        assert(psGetCalls==failedGets&&psSetCalls==failedSets&&workerStarts==1&&bleBegins==0);
      }else{
        assert(bleRunning&&bleBegins==1&&!backup_power.owned()&&!backup_power.faulted()&&observedPs==expectedBeforeBle&&baselineProofs>0);
        assertNormalOrder(beginning,expectedBeforeBle);
        if(scenario=="fresh-driver-cycle"){
          command("BENCH_CLOSE");assert(!bleRunning&&!backup_power.owned());
          // Model the next initialized driver having its own prior policy.
          observedPs=WIFI_PS_MAX_MODEM;expectedBeforeBle=observedPs;tick(501);
          assert(workerStarts==2&&backup_power.previous()==2&&backup_power.owned()&&observedPs==WIFI_PS_NONE);
          const auto next=events.size();openWindow();drained();tick();
          assert(bleBegins==2&&observedPs==WIFI_PS_MAX_MODEM&&!backup_power.owned());
          assertNormalOrder(next,WIFI_PS_MAX_MODEM);
        }else assert(scenario=="normal-min"||scenario=="normal-max"||scenario=="normal-none"||scenario=="policy-status");
        if(scenario=="policy-status"){
          const auto reads=psGetCalls,writes=psSetCalls;
          assert(readPowerCommand("POWER_SAVE_STATUS")["accepted"]==false);
          assert(psGetCalls==reads&&psSetCalls==writes); // Existing BLE SDK gate stays shut.
        }
      }
    }
  }
  // Exercise the RAM-only diagnostic in ordinary, BLE-active and failed states.
  assertRamPolicyStatus();
  assert(testNvs==nvsBefore&&nvsWrites==writes&&usbInitializations==1&&testHandles.empty());
  std::cout<<"PASS actual-runtime power lease "<<scenario<<"; synthetic hardware only\n";
}
