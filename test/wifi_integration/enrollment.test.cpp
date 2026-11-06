// Actual M2 main/runtime/NVS adapter; physical/time/USB/radio/BLE SDK stubbed.
#include "stubs/common.h"
#include "wifi_setup_radio.h"
#include "wifi_setup_runtime.h"
#include "wifi_setup_nvs.h"
#include "ble_management.h"
#include "usb_storage.h"
#include "lcd.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
uint32_t testNow=100000;
unsigned nextHandle=0,nvsWrites=0,usbInitializations=0,usbPolls=0;
bool testOwnerDown=false,testStorageReady=false,testMediaDetected=false,testAuthorized=false,failWrites=false,loseAck=false,loseActiveAck=false;
int simulatedStopCalls=0;bool simulatedStopOk=true;
std::map<std::string,std::vector<uint8_t>> testNvs;
std::map<unsigned,std::string> testHandles,testLastKey;
FakeWiFi WiFi;FakeSerial USBSerial;
bool testPlatformReady=true,bleRunning=false,releaseOk=true,reinitOk=true;unsigned bleBegins=0;
std::vector<std::string> lifecycleEvents;
bool ble_platform_prepare(){return testPlatformReady;}
bool ble_platform_ready(){return testPlatformReady;}
int32_t ble_platform_error(){return -1;}
bool ble_management_begin(){++bleBegins;bleRunning=true;return true;}
void ble_management_poll(){}
bool ble_management_stop(){lifecycleEvents.push_back("ble-stop");if(simulatedStopOk)bleRunning=false;return simulatedStopOk;}
bool ble_management_active(){return bleRunning;}
bool ble_management_authenticated(){return false;}
int32_t ble_management_last_error(){return 0;}
BleManagementStatus ble_management_status(){return {};}
void ble_management_set_connection_status(bool){}
bool ble_management_authorizes(const WifiSetupSession&){return false;}
extern "C" uint32_t csp_ble_sdk_host_stack_min_bytes(){return 2048;}
void init_usb_storage(bool){++usbInitializations;}
void poll_storage(){++usbPolls;}
pro::Owner storage_owner(){return pro::Owner::Host;}
bool storage_ready(){return testStorageReady;}
bool storage_media_detected(){return testStorageReady||testMediaDetected;}
void lcd_init(){}
void lcd_show_loading(){}
void lcd_show_text(const char*){}
WifiSetupRadioSnapshot radio;
bool wifi_setup_radio_init(){lifecycleEvents.push_back("radio-init");return reinitOk;}
bool wifi_setup_radio_release(){lifecycleEvents.push_back("radio-release");assert(!bleRunning);return releaseOk;}
bool wifi_setup_radio_scan(){return false;}
bool wifi_setup_radio_connect(const pro::wifi_setup::CheckProfile&){return false;}
bool wifi_setup_radio_disconnect(){return true;}
void wifi_setup_radio_poll(){}
const WifiSetupRadioSnapshot& wifi_setup_radio_snapshot(){return radio;}
#include "../../src/main.cpp"
void command(const std::string& text){testNow+=300;USBSerial.send(text);while(USBSerial.available())loop();}
int main(int argc,char** argv){
  assert(argc==3);std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> record{std::istreambuf_iterator<char>(file),{}};
  std::string scenario=argv[2];const auto requested=scenario;
  const bool cleanupCase=scenario=="release-fail"||scenario=="reinit-fail"||scenario=="ble-stop-fail"||scenario=="release-memory-fail";
  if(cleanupCase)scenario="normal";
  assert(record.size()==432);
  if(scenario=="owner-corrupt"){testNvs["csp-owner/owner"]=record;testNvs["csp-owner/owner"][0]^=1;}
  if(scenario=="init-fail")testPlatformReady=false;
  if(scenario=="record-invalid")record[0]^=1;
  setup();assert(usbInitializations==1&&nvsWrites==0&&bleBegins==0);
  std::string installation="OWNER_INSTALL ";for(auto b:record){char pair[3];std::snprintf(pair,sizeof pair,"%02x",b);installation+=pair;}
  // An unsolicited serial message never enrolls and never opens BLE.
  command(installation);assert(nvsWrites==0&&bleBegins==0);
  command("OWNER_STATUS");
  assert(USBSerial.lines.back().find("\"boot_down\":false")!=std::string::npos);
  assert(USBSerial.lines.back().find("\"can_enroll\":false")!=std::string::npos);
  testOwnerDown=true;loop();testNow+=scenario=="held-short"?9000:10001;
  if(scenario=="media-present")testStorageReady=true;
  if(scenario=="host-media-unmounted"){testMediaDetected=true;assert(!storage_ready());}
  if(scenario=="radio-busy")radio.state=WifiSetupRadioState::Connecting;
  if(scenario=="lost-ack")loseAck=true;
  command("OWNER_STATUS");
  if(scenario=="normal"||scenario=="lost-ack"||scenario=="record-invalid")
    assert(USBSerial.lines.back().find("\"can_enroll\":true")!=std::string::npos);
  else assert(USBSerial.lines.back().find("\"can_enroll\":false")!=std::string::npos);
  command(installation);
  const bool success=scenario=="normal";
  if(success){
    assert(wifi_owner_install_step()==WifiOwnerInstallStep::Verified);
    assert(nvsWrites==1&&testNvs.at("csp-owner/owner")==record);
    assert(USBSerial.lines.back()=="{\"owner_installed\":true}");
    assert(bleBegins==0);testNow+=5000;loop();assert(bleBegins==0); // release required
    testOwnerDown=false;loop();testOwnerDown=true;loop();testNow+=3001;loop();assert(bleBegins==1);
    const auto saved=testNvs["csp-owner/owner"];command(installation);assert(nvsWrites==1&&testNvs["csp-owner/owner"]==saved);
    assert(USBSerial.lines.back()=="{\"owner_installed\":false}");
    protocomm_security2_params_t p{};assert(wifi_setup_owner_parameters(p));assert(p.salt_len==16&&p.verifier_len==384);
  }else{
    assert(USBSerial.lines.back()=="{\"owner_installed\":false}");
    if(scenario=="lost-ack"){assert(nvsWrites==1);protocomm_security2_params_t p{};assert(!wifi_setup_owner_parameters(p));}
    else assert(nvsWrites==0);
    assert(bleBegins==0);
    if(scenario=="record-invalid")assert(wifi_owner_install_step()==WifiOwnerInstallStep::Record);
    if(scenario=="lost-ack")assert(wifi_owner_install_step()==WifiOwnerInstallStep::Write);
  }
  for(const auto& line:USBSerial.lines){assert(line.find(installation)==std::string::npos);assert(line.find("owner_key")==std::string::npos);}
  assert(usbPolls>0&&testHandles.empty());
  assert(!testNvs.count("csp/config")&&!testNvs.count("csp-wifi2/active"));
  // Read-only restart evidence must be valid, current and must not mutate NVS,
  // open BLE or change USB enumeration. SDK reset cause is simulated here.
  const auto writesBefore=nvsWrites,beginsBefore=bleBegins,usbBefore=usbInitializations;
  const auto nvsBefore=testNvs;
  command("BOOT_STATUS");
  JsonDocument boot;assert(!deserializeJson(boot,USBSerial.lines.back()));
  assert(boot.size()==2&&boot["reset_reason"].as<unsigned>()==4);
  const auto firstUptime=boot["uptime_ms"].as<uint32_t>();
  assert(firstUptime<=testNow&&testNow-firstUptime<20);
  command("BOOT_STATUS");assert(!deserializeJson(boot,USBSerial.lines.back()));
  assert(boot["uptime_ms"].as<uint32_t>()>firstUptime);
  assert(boot["reset_reason"].as<unsigned>()==4);
  assert(nvsWrites==writesBefore&&testNvs==nvsBefore&&bleBegins==beginsBefore&&usbInitializations==usbBefore);
#if CSP_USB_BENCH_AUTOMATION
  if(success){
    command("BENCH_WINDOW");assert(USBSerial.lines.back()=="{\"bench_window\":false}");
    command("BENCH_CLOSE");assert(USBSerial.lines.back()=="{\"bench_close\":true}"&&!bleRunning);
#if CSP_BENCH_RADIO_RELEASE
    const auto pollsDuringRelease=usbPolls;
    loop();assert(usbPolls>pollsDuringRelease); // asynchronous release keeps owner USB polling alive
    testNow+=501;loop();
    assert(lifecycleEvents.size()>=3);
    assert((std::vector<std::string>(lifecycleEvents.end()-3,lifecycleEvents.end())==std::vector<std::string>{"ble-stop","radio-release","radio-init"}));
#endif
    testStorageReady=true;command("BENCH_WINDOW");assert(USBSerial.lines.back()=="{\"bench_window\":false}");
    testStorageReady=false;command("BENCH_WINDOW");assert(USBSerial.lines.back()=="{\"bench_window\":true}"&&bleRunning);
    if(requested=="release-fail")releaseOk=false;
    if(requested=="reinit-fail")reinitOk=false;
    if(requested=="ble-stop-fail")simulatedStopOk=false;
    if(requested=="release-memory-fail")testLargestBlock=64000;
    const auto eventsBefore=lifecycleEvents.size();
    command("BENCH_CLOSE");assert(USBSerial.lines.back()=="{\"bench_close\":true}");
    assert(bleRunning==(requested=="ble-stop-fail"));
#if CSP_BENCH_RADIO_RELEASE
    testNow+=requested=="release-memory-fail"?2001:501;loop();
    const size_t expectedEvents=requested=="ble-stop-fail"?1:requested=="release-fail"||requested=="release-memory-fail"?2:3;
    assert(lifecycleEvents.size()==eventsBefore+expectedEvents);
    if(cleanupCase){command("BENCH_WINDOW");assert(USBSerial.lines.back()=="{\"bench_window\":false}");}
#else
    (void)eventsBefore;
#endif
  }else{
    command("BENCH_WINDOW");assert(USBSerial.lines.back()=="{\"bench_window\":false}");
  }
#else
  command("BENCH_WINDOW");assert(USBSerial.lines.back()=="{\"accepted\":false}");
  command("BENCH_CLOSE");assert(USBSerial.lines.back()=="{\"accepted\":false}");
#endif
  assert(nvsWrites==writesBefore&&testNvs==nvsBefore&&usbInitializations==usbBefore);
  std::cout<<"PASS first enrollment "<<requested<<"; USB alive; existing credentials untouched\n";
}
