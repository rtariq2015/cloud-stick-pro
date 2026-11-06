// Compiles actual main + isolated runtime. Hardware/library interfaces are
// stubs; real lifecycle policy, diagnostics parser and ArduinoJson are used.
#include <cassert>
#include <iostream>
#include "isolation_stubs/common.h"
#include "ble_management.h"
uint32_t testNow=0;bool testButtonDown=false;
unsigned usbInitializations=0,usbPolls=0,forbiddenCalls=0,resets=0,diagnosticPolls=0;
size_t testFreeHeap=500000,testLargestHeap=250000;
FakeSerial USBSerial;FakeESP ESP;FakeWiFi WiFi;
unsigned beginCalls=0,stopCalls=0,realFlashWrites=0;
bool testPreparationOk=true;
unsigned prepareCalls=0;
bool ble_platform_prepare(){assert(usbInitializations==0);++prepareCalls;return testPreparationOk;}
bool ble_platform_ready(){return testPreparationOk;}
int32_t ble_platform_error(){return testPreparationOk?0:-2503;}
bool simulatedActive=false,simulatedStopOk=true;
bool ble_management_begin(){++beginCalls;simulatedActive=true;return true;}
void ble_management_poll(){}
bool ble_management_stop(){++stopCalls;if(simulatedStopOk)simulatedActive=false;return simulatedStopOk;}
bool ble_management_active(){return simulatedActive;}
bool ble_management_authenticated(){return false;}
int32_t ble_management_last_error(){return 0;}
BleManagementStatus ble_management_status(){BleManagementStatus s;s.active=simulatedActive;return s;}
void ble_management_set_connection_status(bool wifi){assert(!wifi);}
bool csp_ble_sdk_ready(){return simulatedActive;}
bool csp_ble_sdk_idle(){return !simulatedActive;}
uint32_t csp_ble_sdk_host_stack_min_bytes(){return simulatedActive?0:2048;}
extern "C" esp_err_t __real_esp_partition_write(const esp_partition_t*,size_t,const void*,size_t){++realFlashWrites;return ESP_OK;}
extern "C" esp_err_t __real_esp_partition_write_raw(const esp_partition_t*,size_t,const void*,size_t){++realFlashWrites;return ESP_OK;}
extern "C" esp_err_t __real_esp_partition_erase_range(const esp_partition_t*,size_t,size_t){++realFlashWrites;return ESP_OK;}
#include "../../src/main.cpp"
#include "../../src/ble_isolated_runtime.cpp"
#include "../../src/ble_isolated_flash_guard.cpp"
static void tick(uint32_t at,bool down=false){testNow=at;testButtonDown=down;loop();}
static void command(const char* text){
 USBSerial.send(text);testNow+=250;
 while(USBSerial.available())loop();
}
static void assertContained(){
 assert(forbiddenCalls==0 && resets==0 && usbInitializations==1 && usbPolls>0);
}
int main(int argc,char** argv){
 (void)buttonDown;(void)lastReconnect;(void)maintenanceRequested;
 assert(argc==2);std::string scenario=argv[1];
 if(scenario=="prepare-failed")testPreparationOk=false;
 setup();assert(usbInitializations==1 && beginCalls==0 && forbiddenCalls==0);
 assert(prepareCalls==1);
 if(scenario=="prepare-failed"){
  for(unsigned n=0;n<20;++n)tick(1000+n*1000,true);
  command("STATUS");assert(beginCalls==0&&!simulatedActive);assertContained();
  assert(USBSerial.lines.back().find(R"("ble_platform_ready":false)")!=std::string::npos);
  assert(USBSerial.lines.back().find(R"("ble_platform_error":-2503)")!=std::string::npos);
  std::cout<<"PASS preparation failure preserves USB and denies physical BLE without retry/reset\n";
  return 0;
 }
 for(unsigned n=0;n<10;++n)tick(1000+n*100);
 request_setup();resume_saved_backups();restart_after_eject();
 command("CHECK");command("TEST");command("SETUP");
 command("CHECKXXXXXXXXXXXXXXXXXXXXXXXXXXXSETUP");
 assert(beginCalls==0 && !simulatedActive);assertContained();
 if(scenario=="normal"){
  const esp_partition_t partition{};
  uint8_t value=0x42;
  assert(__wrap_esp_partition_write(&partition,0,&value,1)==ESP_ERR_NOT_SUPPORTED);
  assert(__wrap_esp_partition_write_raw(&partition,0,&value,1)==ESP_ERR_NOT_SUPPORTED);
  assert(__wrap_esp_partition_erase_range(&partition,0,4096)==ESP_ERR_NOT_SUPPORTED);
  assert(realFlashWrites==0 && rejectedFlashWrites.load()==3);
  esp_flash_t flash{};
  assert(__wrap_esp_flash_write(&flash,&value,0,1)==ESP_ERR_NOT_SUPPORTED);
  assert(__wrap_esp_flash_write_encrypted(&flash,0,&value,1)==ESP_ERR_NOT_SUPPORTED);
  assert(__wrap_esp_flash_erase_region(&flash,0,4096)==ESP_ERR_NOT_SUPPORTED);
  assert(__wrap_esp_flash_erase_chip(&flash)==ESP_ERR_NOT_SUPPORTED);
  assert(realFlashWrites==0 && ble_isolated_raw_flash_denials()==4);
  command("STATUS");
  JsonDocument info;assert(!deserializeJson(info,USBSerial.lines.back()));
  const std::string& telemetry=USBSerial.lines.back();
  for(const char* field:{R"("isolated_test":true)",R"("diagnostics_persistent":false)",
      R"("wifi_started":false)",R"("backup_worker_started":false)",
      R"("receipt_worker_started":false)",R"("configured":false)",
      R"("rejected_flash_writes":3)",R"("rejected_raw_flash_writes":4)",
      R"("loop_stack_free_bytes":1234)",R"("ble_host_stack_min_bytes_after_exit":2048)"})
    assert(telemetry.find(field)!=std::string::npos);
  command("LOG");assert(USBSerial.lines.back().find("events")!=std::string::npos);
 }else if(scenario=="failed-stop")simulatedStopOk=false;
 else if(scenario=="memory-not-restored"){}
 else return 2;
 tick(10000,true);tick(12999,true);assert(beginCalls==0);
 tick(13000,true);assert(beginCalls==1 && simulatedActive && maintenance_active());
 tick(20000,true);restart_after_eject();assert(beginCalls==1 && resets==0);
 if(scenario=="memory-not-restored"){
  // Similar to the observed target: nearly all total heap returns, but the
  // largest contiguous block remains too small relative to the start sample.
  testFreeHeap=498884;testLargestHeap=139252;
 }
 tick(133000,true);assert(stopCalls==1);
 command("STATUS");
 JsonDocument recovery;assert(!deserializeJson(recovery,USBSerial.lines.back()));
 assert(recovery["ble_before_free_bytes"].as<uint32_t>()==500000);
 assert(recovery["ble_before_largest_bytes"].as<uint32_t>()==250000);
 assert(recovery["ble_after_free_bytes"].as<uint32_t>()==testFreeHeap);
 assert(recovery["ble_after_largest_bytes"].as<uint32_t>()==testLargestHeap);
 assert(recovery["ble_restore_checked"].as<bool>());
 assert(recovery["ble_stop_confirmed"].as<bool>()==simulatedStopOk);
 assert(recovery["ble_memory_restored"].as<bool>()==(scenario!="memory-not-restored"));
 if(scenario=="normal"){
  assert(!simulatedActive && !maintenance_active());
  tick(134000,true);assert(beginCalls==1); // Continuous hold cannot reopen.
  tick(135000,false);tick(136000,true);tick(139000,true);assert(beginCalls==2);
 }else{
  assert(bleLifecycle.state()==pro::BleLifecycle::State::Blocked && maintenance_active());
  tick(134000,false);tick(135000,true);tick(138000,true);
  assert(beginCalls==1 && stopCalls==1); // No retry/reset after failed recovery.
  assert(bleLifecycle.reason()==(scenario=="failed-stop"?
    pro::BleLifecycle::Reason::StopFailed:pro::BleLifecycle::Reason::MemoryNotRestored));
 }
 assertContained();
 std::cout<<"PASS actual isolated main/runtime containment: "<<scenario<<std::endl;
 return 0;
}
