// Actual combined owner runtime/NVS/policy. RTOS worker, USB, BLE and radio are
// controlled fixtures; no physical GATT, filesystem, TLS or account is used.
#include "runtime_stubs.h"
#include "wifi_setup_radio.h"
#include "wifi_setup_runtime.h"
#include "wifi_setup_nvs.h"
#include "ble_management.h"
#include "backup_worker.h"
#include "backup_memory.h"
#include "pro_ble_message.h"
#include <wifi_wire.h>
#include "usb_storage.h"
#include "lcd.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
#include <algorithm>
#include <climits>
#include <set>
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
#include "wifi_setup_nvs_release.h"
#include <esp_partition.h>
#include <nvs_flash.h>
#endif

uint32_t testNow=100000;
size_t runtimeFreeHeap=500000;
unsigned nextHandle=0,nvsWrites=0,usbInitializations=0,usbPolls=0;
bool testOwnerDown=false,testStorageReady=false,testMediaDetected=true,testAuthorized=false;
bool failWrites=false,loseAck=false,loseActiveAck=false;
int simulatedStopCalls=0;bool simulatedStopOk=true;
std::map<std::string,std::vector<uint8_t>> testNvs;
std::map<unsigned,std::string> testHandles,testLastKey;
FakeWiFi WiFi;FakeSerial USBSerial;
bool platformReady=true,bleRunning=false,releaseOk=true,initOk=true,configValid=true;
bool maintenance=true,reapReady=false,startOk=true;
unsigned bleBegins=0,retireRequests=0,reapCalls=0,workerStarts=0,configReads=0,radioReleases=0,clockCalls=0;
unsigned cloudChecks=0,cloudUploadTests=0;
unsigned radioConnections=0;
bool asynchronousFailover=false;
std::vector<uint32_t> connectionAttempts;
CloudConfig lastStartedConfig;
BackupWorkerSnapshot managed;
WifiSetupRadioSnapshot radio;
std::vector<std::string> events;
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
std::array<uint8_t,0x5000> releaseFlash{};
esp_partition_t releasePartition{0x9000,0x5000};
int releaseDeinitError=0,releaseInitError=0;
unsigned nvsReleaseCalls=0;
bool releaseMutatesFlash=false,releaseRestoresHeap=true;
const esp_partition_t* esp_partition_find_first(int type,int subtype,const char* name){
  assert(type==ESP_PARTITION_TYPE_DATA&&subtype==ESP_PARTITION_SUBTYPE_DATA_NVS&&std::strcmp(name,"nvs")==0);
  assert(!bleRunning&&managed.phase==BackupWorkerPhase::Stopped&&radioReleases>0);
  return &releasePartition;
}
esp_err_t esp_partition_read(const esp_partition_t* p,size_t offset,void* out,size_t bytes){
  assert(p==&releasePartition&&offset+bytes<=releaseFlash.size());
  std::memcpy(out,releaseFlash.data()+offset,bytes);return ESP_OK;
}
esp_err_t nvs_flash_deinit(){
  assert(!bleRunning&&managed.phase==BackupWorkerPhase::Stopped);
  ++nvsReleaseCalls;events.emplace_back("nvs-deinit");return releaseDeinitError;
}
esp_err_t nvs_flash_init(){
  ++nvsReleaseCalls;events.emplace_back("nvs-init");
  if(releaseMutatesFlash)releaseFlash.back()^=1;
  if(releaseRestoresHeap)testLargestBlock=250000;
  return releaseInitError;
}
#endif
std::string lastScreen;
BackupStatus observedBackup;
size_t applicationPlaintext=0,applicationEnvelope=0;
std::string applicationRaw;
std::string timeServerFixture="pool.ntp.org";
const char* borrowedTimeServer=nullptr;
// Memory sampling has its own real-adapter tests. This runtime fixture only
// supplies nonsecret observations while exercising owner-loop dispatch.
void backup_memory_init(){}
void backup_memory_poll(uint32_t){}
BackupMemorySnapshot backup_memory_snapshot(){return {true,1,32000,16000,0};}

bool ble_platform_prepare(){return platformReady;}
bool ble_platform_ready(){return platformReady;}
int32_t ble_platform_error(){return -1;}
bool ble_management_begin(){
  assert(managed.phase==BackupWorkerPhase::Stopped&&maintenance);
  events.emplace_back("ble-start");++bleBegins;bleRunning=true;return true;
}
void ble_management_poll(){}
bool ble_management_stop(){events.emplace_back("ble-stop");++simulatedStopCalls;if(simulatedStopOk)bleRunning=false;return simulatedStopOk;}
bool ble_management_active(){return bleRunning;}
bool ble_management_authenticated(){return bleRunning&&testAuthorized;}
int32_t ble_management_last_error(){return 0;}
BleManagementStatus ble_management_status(){return {};}
void ble_management_set_connection_status(bool){}
bool ble_management_authorizes(const WifiSetupSession& s){return bleRunning&&testAuthorized&&s.id==7&&s.generation==9;}
extern "C" uint32_t csp_ble_sdk_host_stack_min_bytes(){return 2048;}
void init_usb_storage(bool){++usbInitializations;}
void poll_storage(){++usbPolls;}
pro::Owner storage_owner(){return pro::Owner::Host;}
bool storage_ready(){return testStorageReady;}
bool storage_media_detected(){return testMediaDetected;}
void lcd_init(){}
void lcd_show_loading(){}
void lcd_show_text(const char* text){lastScreen=text;}
void lcd_pro_update(const BackupStatus&,pro::Owner,bool){}
bool wifi_setup_radio_init(){events.emplace_back("radio-init");return initOk;}
bool wifi_setup_radio_release(){
  assert(!bleRunning&&managed.phase==BackupWorkerPhase::Stopped);
  events.emplace_back("radio-release");++radioReleases;radio.state=WifiSetupRadioState::Idle;return releaseOk;
}
bool wifi_setup_radio_scan(){radio.state=WifiSetupRadioState::Scanning;return true;}
bool wifi_setup_radio_connect(const pro::wifi_setup::CheckProfile& p){
  ++radioConnections;connectionAttempts.push_back(p.id);radio.profile=p.id;
  if(asynchronousFailover){assert(!bleRunning&&managed.phase==BackupWorkerPhase::Stopped);}
  radio.state=asynchronousFailover&&p.id!=8?WifiSetupRadioState::Connecting:WifiSetupRadioState::LocalConnected;
  return true;
}
bool wifi_setup_radio_disconnect(){radio.state=WifiSetupRadioState::Idle;return true;}
void wifi_setup_radio_poll(){}
const WifiSetupRadioSnapshot& wifi_setup_radio_snapshot(){return radio;}
// Pinned lwIP retains this pointer rather than copying the hostname. Deliberately
// borrow it here so ASan can detect a temporary CloudConfig lifetime regression.
void configTzTime(const char*,const char* hostname){++clockCalls;borrowedTimeServer=hostname;}
CloudConfig load_backup_config(){
  ++configReads;CloudConfig c;c.is_valid=configValid;c.server_url="https://synthetic.invalid/backup/";
  c.username="fixture-owner";c.password="synthetic-cloud-password";c.ca_pem="synthetic-ca";
  c.ntp_server=timeServerFixture;return c;
}
BackupStatus backup_status(){return observedBackup;}
void backup_message(const char*,int){}
void set_maintenance(bool value){maintenance=value;events.emplace_back(value?"maintenance-on":"maintenance-off");}
bool maintenance_active(){return maintenance;}
bool request_connection_check(bool upload){
  ++cloudChecks;if(upload)++cloudUploadTests;
  return !maintenance&&!bleRunning&&managed.phase==BackupWorkerPhase::Running;
}
bool backup_worker_start(const CloudConfig& c){
  assert(c.is_valid&&!bleRunning&&managed.phase==BackupWorkerPhase::Stopped);
  lastStartedConfig=c;++workerStarts;events.emplace_back("backup-start");++managed.generation;
  if(!startOk){maintenance=true;managed.phase=BackupWorkerPhase::Failed;managed.error=-2113;return false;}
  maintenance=false;managed.phase=BackupWorkerPhase::Running;managed.error=0;return true;
}
uint32_t backup_worker_request_retire(){
  ++retireRequests;events.emplace_back("backup-retire");maintenance=true;
  if(managed.phase==BackupWorkerPhase::Running||managed.phase==BackupWorkerPhase::Starting)managed.phase=BackupWorkerPhase::Retiring;
  return managed.generation;
}
BackupWorkerSnapshot backup_worker_snapshot(){return managed;}
bool backup_worker_reap(){
  ++reapCalls;
  if(managed.phase==BackupWorkerPhase::Failed){managed.phase=BackupWorkerPhase::Stopped;managed.error=0;events.emplace_back("backup-reap");return true;}
  if(managed.phase!=BackupWorkerPhase::Drained||!reapReady)return false;
  events.emplace_back("backup-reap");managed.phase=BackupWorkerPhase::Stopped;return true;
}
#include "../../src/main.cpp"

void tick(uint32_t ms=10){testNow+=ms;loop();}
void assertTimeServerLifetime(){
  assert(borrowedTimeServer&&clockCalls==1);
  // Churn the same allocation classes after setup's config scope has ended.
  // Unfixed code makes strcmp a heap/stack use-after-free regardless of whether
  // the allocator happens to overwrite the borrowed string's old bytes.
  std::vector<std::string> churn;
  for(unsigned i=0;i<256;++i)churn.emplace_back(timeServerFixture.size(),char('a'+i%26));
  assert(std::strcmp(borrowedTimeServer,timeServerFixture.c_str())==0);
}
JsonDocument command(const std::string& text){
  testNow+=300;const auto before=USBSerial.lines.size();USBSerial.send(text);
  while(USBSerial.available())loop();
  assert(USBSerial.lines.size()>before);
  JsonDocument response;assert(!deserializeJson(response,USBSerial.lines[before]));return response;
}
void drained(bool parked=true){managed.phase=BackupWorkerPhase::Drained;managed.retired_generation=managed.generation;reapReady=parked;}
void openWindow(){auto result=command("BENCH_WINDOW");assert(result["bench_window"]==true);}
void enterBle(){
  const auto starts=bleBegins;openWindow();assert(!bleRunning&&bleBegins==starts&&maintenance);
  drained();tick();assert(bleRunning&&bleBegins==starts+1);
}
void closeBle(){
  const auto starts=workerStarts;auto result=command("BENCH_CLOSE");assert(result["bench_close"]==true);
  assert(!bleRunning&&maintenance&&workerStarts==starts);tick(501);
  assert(!maintenance&&workerStarts==starts+1&&managed.phase==BackupWorkerPhase::Running);
}
JsonDocument application(std::string input){
  static unsigned id=1;
  JsonDocument request;assert(!deserializeJson(request,input));request["v"]=2;request["id"]=id++;
  input.clear();serializeJson(request,input);
  pro::wifi_setup::WireRequest parsed;
  assert(pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(input.data()),input.size(),parsed));
  uint8_t* output=nullptr;ssize_t length=0;const WifiSetupSession session{7,9};
  assert(wifi_setup_endpoint(session,parsed,reinterpret_cast<const uint8_t*>(input.data()),input.size(),&output,&length)==ESP_OK);
  JsonDocument queued;assert(!deserializeJson(queued,output,size_t(length)));std::free(output);
  assert(queued["ok"]==true);const auto job=queued["job"].as<unsigned>();tick();
  const std::string poll="{\"v\":2,\"id\":"+std::to_string(id++)+",\"op\":\"result\",\"job\":"+std::to_string(job)+"}";
  pro::wifi_setup::WireRequest result;assert(pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(poll.data()),poll.size(),result));
  output=nullptr;length=0;
  assert(wifi_setup_endpoint(session,result,reinterpret_cast<const uint8_t*>(poll.data()),poll.size(),&output,&length)==ESP_OK);
  applicationEnvelope=size_t(length);applicationRaw.assign(reinterpret_cast<const char*>(output),size_t(length));
  JsonDocument finished;assert(!deserializeJson(finished,output,size_t(length)));std::free(output);
  assert(finished["pending"]==false);JsonDocument data;data.set(finished["data"]);
  applicationPlaintext=measureJson(data);assert(applicationPlaintext<pro::wifi_setup::WifiResultLimit&&applicationEnvelope<pro::ble::ResponseLimit);
  return data;
}

int main(int argc,char** argv){
  assert(argc==3);const std::string scenario=argv[2];
  std::ifstream source(argv[1],std::ios::binary);std::vector<uint8_t> owner{std::istreambuf_iterator<char>(source),{}};assert(owner.size()==432);
  if(scenario!="owner-missing")testNvs["csp-owner/owner"]=owner;
  if(scenario=="owner-corrupt")testNvs["csp-owner/owner"][0]^=1;
  const std::string cloudBlob="{\"schema\":1,\"mode\":\"upload_only\",\"private_fixture\":\"keep-exact\"}";
  testNvs["csp/config"]={cloudBlob.begin(),cloudBlob.end()};
  pro::wifi_setup::StoredNetworks saved;saved.revision=2;saved.count=1;saved.selected=1;saved.next_id=2;
  auto& profile=saved.profiles[0];profile.id=1;std::strcpy(profile.ssid.data(),"Synthetic AP");std::strcpy(profile.secret.data(),"synthetic-wifi-password");
  if(scenario=="wifi-failover"){
    saved.count=2;saved.next_id=3;saved.profiles[1]=profile;saved.profiles[1].id=2;
    std::strcpy(saved.profiles[1].ssid.data(),"Synthetic fallback");saved.profiles[1].priority=20;
  }
  if(scenario=="wifi-eight-saved"){
    saved.count=8;saved.next_id=9;
    for(unsigned i=1;i<8;++i){
      saved.profiles[i]=profile;saved.profiles[i].id=i+1;saved.profiles[i].priority=8-i;
      const auto name=std::string("Synthetic fallback ")+std::to_string(i+1);
      std::strcpy(saved.profiles[i].ssid.data(),name.c_str());
    }
  }
  WifiNvsStore nvs;pro::wifi_setup::ProfileStore fixture(nvs);
  assert(fixture.publish(saved,1,[](void*){return true;},nullptr)==pro::wifi_setup::Published::Committed);
  const auto nvsBefore=testNvs;const auto writesBefore=nvsWrites;
  if(scenario=="config-invalid")configValid=false;
  if(scenario=="init-fail")initOk=false;
  if(scenario=="start-failure")startOk=false;
  if(scenario=="ntp-long-lived")timeServerFixture=std::string(40,'n')+"."+std::string(39,'m')+".invalid";
  if(scenario=="ntp-max-bound")timeServerFixture=std::string(60,'n')+"."+std::string(59,'m')+".invalid";
  if(scenario=="ntp-empty")timeServerFixture="";
  if(scenario=="ntp-too-long")timeServerFixture=std::string(121,'n')+".invalid";
  setup();assert(usbInitializations==1&&bleBegins==0&&nvsWrites==writesBefore&&maintenance);tick();
  const bool noStart=scenario=="owner-missing"||scenario=="owner-corrupt"||scenario=="config-invalid"||scenario=="init-fail"||scenario=="ntp-empty"||scenario=="ntp-too-long";
  if(noStart){
    assert(workerStarts==0&&maintenance);tick(60000);assert(workerStarts==0&&maintenance);
    if(scenario=="ntp-empty"||scenario=="ntp-too-long")assert(clockCalls==0&&!borrowedTimeServer);
  }else if(scenario=="start-failure"){
    assert(workerStarts==1&&managed.phase==BackupWorkerPhase::Failed&&maintenance);
    startOk=true;tick(5001);
    assert(workerStarts==2&&managed.phase==BackupWorkerPhase::Running&&!maintenance);
  }else{
    assert(workerStarts==1&&managed.phase==BackupWorkerPhase::Running&&!maintenance);
    if(scenario=="wifi-eight-saved"){
      assert(radioConnections==1&&radio.profile==1);asynchronousFailover=true;
      radio.state=WifiSetupRadioState::Failed;
      tick(1000);assert(maintenance&&managed.phase==BackupWorkerPhase::Retiring);
      tick(1000);assert(radioConnections==1);
      drained(false);tick(1000);assert(radioConnections==1);
      reapReady=true;tick(1000);assert(radio.profile==2&&radioConnections==2);
      for(unsigned id=2;id<8;++id){
        assert(radio.state==WifiSetupRadioState::Connecting&&maintenance&&workerStarts==1);
        tick(24999);assert(radio.profile==id&&radioConnections==id);
        radio.state=WifiSetupRadioState::Failed;tick(1000);
        assert(radio.profile==id+1&&radioConnections==id+1);
      }
      assert((connectionAttempts==std::vector<uint32_t>{1,2,3,4,5,6,7,8}));
      assert(workerStarts==2&&!maintenance&&radio.state==WifiSetupRadioState::LocalConnected);
      tick(60000);assert(radio.profile==8&&radioConnections==8&&workerStarts==2);
    }else if(scenario=="wifi-reconnect"||scenario=="wifi-failover"){
      assert(radioConnections==1);radio.state=WifiSetupRadioState::Failed;
      tick(1000);assert(maintenance&&managed.phase==BackupWorkerPhase::Retiring);
      tick(1000);assert(radioConnections==1&&workerStarts==1);
      drained(false);tick(1000);assert(radioConnections==1&&workerStarts==1);
      reapReady=true;tick(1000);
      if(scenario=="wifi-reconnect"){
        assert(maintenance&&workerStarts==1&&radioConnections==1);
        tick(10001);assert(radio.profile==1);
      }else assert(radio.profile==2); // Saved selection remains1; no NVS mutation.
      assert(workerStarts==2&&radioConnections==2&&!maintenance&&!bleRunning&&bleBegins==0);
    }else if(scenario=="ntp-short-lived"||scenario=="ntp-long-lived"||scenario=="ntp-max-bound"){
      assertTimeServerLifetime();enterBle();assertTimeServerLifetime();closeBle();assertTimeServerLifetime();
    }else if(scenario=="normal"||scenario=="repeated"){
      const unsigned cycles=scenario=="repeated"?3:1;
      for(unsigned i=0;i<cycles;++i){
        const auto starts=workerStarts;openWindow();assert(maintenance&&retireRequests==i+1&&bleBegins==i);
        for(unsigned n=0;n<5;++n)tick(100);assert(bleBegins==i&&workerStarts==starts);
        drained(false);tick();assert(bleBegins==i&&workerStarts==starts&&reapCalls>0);
        reapReady=true;tick();assert(bleBegins==i+1&&bleRunning&&workerStarts==starts);closeBle();
      }
      assert(workerStarts==1+cycles&&radioReleases==cycles);
    }else if(scenario=="quiesce-timeout"||scenario=="cancel-retiring"||scenario=="stale-retirement"||scenario=="reap-timeout"){
      openWindow();assert(maintenance&&managed.phase==BackupWorkerPhase::Retiring);
      if(scenario=="stale-retirement"){drained();managed.retired_generation=managed.generation-1;}
      if(scenario=="reap-timeout")drained(false);
      if(scenario=="cancel-retiring")command("BENCH_CLOSE");else tick(45001);
      tick(60000);assert(bleBegins==0&&radioReleases==0&&simulatedStopCalls==0&&workerStarts==1&&maintenance);
    }else if(scenario=="heap-start-low"){
      openWindow();drained();runtimeFreeHeap=160*1024-1;tick();
      assert(bleBegins==0&&maintenance);tick(31000);assert(workerStarts==1&&maintenance);
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
    }else if(scenario=="nvs-release-normal"||scenario=="nvs-deinit-fail"||scenario=="nvs-init-fail"||scenario=="nvs-flash-changed"||scenario=="nvs-heap-low"){
      enterBle();testLargestBlock=64000;
      if(scenario=="nvs-deinit-fail")releaseDeinitError=-42;
      if(scenario=="nvs-init-fail")releaseInitError=-43;
      if(scenario=="nvs-flash-changed")releaseMutatesFlash=true;
      if(scenario=="nvs-heap-low")releaseRestoresHeap=false;
      const auto inits=std::count(events.begin(),events.end(),"radio-init");
      command("BENCH_CLOSE");tick(501);
      const auto n=command("NVS_RELEASE_STATUS");
      if(scenario=="nvs-release-normal"){
        assert(n["flash_equal"]==true&&n["before_largest"]==64000&&n["after_largest"]==250000);
        assert(!maintenance&&workerStarts==2&&managed.phase==BackupWorkerPhase::Running&&nvsReleaseCalls==2);
        assert(std::find(events.begin(),events.end(),"radio-release")<std::find(events.begin(),events.end(),"nvs-deinit"));
        assert(std::find(events.begin(),events.end(),"nvs-deinit")<std::find(events.begin(),events.end(),"nvs-init"));
        assert(std::count(events.begin(),events.end(),"radio-init")==inits+1);
      }else{
        tick(31000);assert(maintenance&&workerStarts==1);
        assert(std::count(events.begin(),events.end(),"radio-init")==inits);
        assert(nvsReleaseCalls==(scenario=="nvs-deinit-fail"?1:2));
        assert(n["flash_equal"]==(scenario=="nvs-heap-low"));
        testLargestBlock=250000;tick(1000);
        assert(command("BENCH_WINDOW")["bench_window"]==false);
      }
#endif
#if CSP_ENABLE_BLE_EXTENDED_SETTLEMENT
    }else if(scenario=="release-slow"||scenario=="release-slow-rollover"||scenario=="release-slow-deadline"||
             scenario=="release-late-network"||scenario=="release-late-network-deadline"){
      enterBle();
      if(scenario=="release-slow-rollover")testNow=UINT32_MAX-1000;
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
      releaseRestoresHeap=false; // successful cache rebuild, delayed external allocation
#endif
      testLargestBlock=64000;command("BENCH_CLOSE");
      const auto polls=usbPolls,connections=radioConnections,writes=nvsWrites;
      const auto inits=std::count(events.begin(),events.end(),"radio-init");
      tick(31000);
      assert(command("RADIO_STATUS")["release_pending"]==true);
      assert(workerStarts==1&&maintenance&&!bleRunning&&usbPolls>polls);
      assert(radioConnections==connections&&std::count(events.begin(),events.end(),"radio-init")==inits);
      if(scenario=="release-slow-deadline"){
        tick(208700); // Exactly 240 seconds, including the status request.
        assert(command("RADIO_STATUS")["release_pending"]==false);
        testLargestBlock=250000;tick(1000);
        assert(workerStarts==1&&maintenance&&radioConnections==connections);
        assert(command("BENCH_WINDOW")["bench_window"]==false);
      }else if(scenario=="release-late-network"||scenario=="release-late-network-deadline"){
        tick(203700); // 235 seconds of proven radio-off memory settlement.
        testLargestBlock=250000;asynchronousFailover=true;tick(100);
        assert(command("RADIO_STATUS")["release_pending"]==false);
        assert(radio.state==WifiSetupRadioState::Connecting&&maintenance&&workerStarts==1);
        if(scenario=="release-late-network"){
          tick(29000);assert(maintenance&&workerStarts==1);
          radio.state=WifiSetupRadioState::LocalConnected;tick(100);
          assert(!maintenance&&workerStarts==2);
        }else{
          tick(29700); // Exactly the separate 30-second network deadline.
          assert(maintenance&&workerStarts==1);
          radio.state=WifiSetupRadioState::LocalConnected;tick(1000);
          assert(maintenance&&workerStarts==1);
        }
      }else{
        tick(148000);testLargestBlock=250000;tick(100);
        assert(command("RADIO_STATUS")["release_pending"]==false);
        assert(!maintenance&&workerStarts==2&&radioConnections==connections+1);
        assert(std::count(events.begin(),events.end(),"radio-init")==inits+1);
      }
      assert(nvsWrites==writes); // waiting never writes settings or receipts
#endif
    }else if(scenario=="release-delayed"||scenario=="release-deadline"||scenario=="release-rollover"){
      enterBle();
      if(scenario=="release-rollover")testNow=UINT32_MAX-1000;
      testLargestBlock=64000;command("BENCH_CLOSE");
      const auto polls=usbPolls,connections=radioConnections;
      const auto inits=std::count(events.begin(),events.end(),"radio-init");
      tick(2100);
      assert(command("RADIO_STATUS")["release_pending"]==true);
      assert(workerStarts==1&&maintenance&&!bleRunning&&usbPolls>polls);
      assert(radioConnections==connections&&std::count(events.begin(),events.end(),"radio-init")==inits);
      if(scenario=="release-deadline"){
        tick(27600); // Exactly the existing 30-second overall closing deadline.
        assert(command("RADIO_STATUS")["release_pending"]==false);
        testLargestBlock=250000;tick(1000);
        assert(workerStarts==1&&maintenance&&radioConnections==connections);
        assert(std::count(events.begin(),events.end(),"radio-init")==inits);
        assert(command("BENCH_WINDOW")["bench_window"]==false);
      }else{
        testLargestBlock=250000;tick(100);
        assert(command("RADIO_STATUS")["release_pending"]==false);
        assert(!maintenance&&workerStarts==2&&managed.phase==BackupWorkerPhase::Running);
        assert(radioConnections==connections+1&&std::count(events.begin(),events.end(),"radio-init")==inits+1);
      }
    }else if(scenario=="ble-stop-fail"||scenario=="release-fail"||scenario=="reinit-fail"||scenario=="heap-return-low"){
      enterBle();
      if(scenario=="ble-stop-fail")simulatedStopOk=false;
      if(scenario=="release-fail")releaseOk=false;
      if(scenario=="reinit-fail")initOk=false;
      command("BENCH_CLOSE");
      if(scenario=="heap-return-low")testLargestBlock=64000;
      tick(2100);tick(31000);assert(workerStarts==1&&maintenance);
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
      if(scenario=="ble-stop-fail"||scenario=="release-fail")assert(nvsReleaseCalls==0);
#endif
      assert(command("BENCH_WINDOW")["bench_window"]==false);
    }else if(scenario=="unauthenticated-api"||scenario=="unauthenticated-backup-status"){
      enterBle();const std::string input=scenario=="unauthenticated-api"?
        "{\"v\":2,\"id\":1,\"op\":\"status\"}":"{\"v\":2,\"id\":1,\"op\":\"backup_status\"}";
      pro::wifi_setup::WireRequest request;assert(pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(input.data()),input.size(),request));
      uint8_t* output=nullptr;ssize_t length=0;
      assert(wifi_setup_endpoint({7,9},request,reinterpret_cast<const uint8_t*>(input.data()),input.size(),&output,&length)==ESP_ERR_INVALID_STATE);
      assert(!output&&length==0&&maintenance);closeBle();
    }else if(scenario=="serial-memory-status"){
      auto status=command("MEMORY_STATUS");
      const std::set<std::string> wanted={"failure_hook_registered","samples","observed_min_free","observed_min_largest","allocation_failure_seen"};
      assert(status.size()==wanted.size());
      for(auto member:status.as<JsonObject>())assert(wanted.count(member.key().c_str())==1);
      assert(status["failure_hook_registered"]==true&&status["samples"]==1&&status["allocation_failure_seen"]==false);
      assert(status["observed_min_free"]==32000&&status["observed_min_largest"]==16000);
    }else if(scenario=="serial-backup-status"){
      observedBackup.lastBackup=253402300799ULL;
      observedBackup.verified=UINT32_MAX;observedBackup.pending=UINT32_MAX;observedBackup.failed=UINT32_MAX;
      observedBackup.error=INT_MIN;observedBackup.hasScan=true;observedBackup.scanComplete=true;
      observedBackup.transferDone=UINT64_MAX;observedBackup.transferTotal=UINT64_MAX;
      observedBackup.transferSpeed=UINT32_MAX;observedBackup.stackFree=4096;observedBackup.minimumHeap=32768;
      std::strcpy(observedBackup.currentFile,"private-synthetic-filename.pdf");
      std::strcpy(observedBackup.message,"synthetic-cloud-password must never be returned");
      const std::set<std::string> wanted={"worker_phase","worker_generation","worker_error","cloud_configured","paused","active",
        "has_scan","scan_complete","verified","pending","failed","last_backup_unix","error","phase","done","total","speed","worker_stack_free","tls_minimum_heap"};
      auto status=command("BACKUP_STATUS");assert(status.size()==wanted.size());
      for(auto member:status.as<JsonObject>())assert(wanted.count(member.key().c_str())==1);
      assert(status["paused"]==false&&status["cloud_configured"]==true);
      assert(status["worker_phase"]==unsigned(BackupWorkerPhase::Running));
      assert(status["last_backup_unix"].as<uint64_t>()==253402300799ULL&&status["error"].as<int>()==INT_MIN);
      assert(status["done"].as<uint64_t>()==UINT64_MAX&&status["total"].as<uint64_t>()==UINT64_MAX);
      std::string rendered;serializeJson(status,rendered);
      assert(rendered.find("private-synthetic-filename")==std::string::npos&&rendered.find("synthetic-cloud-password")==std::string::npos);
      assert(command("CHECK")["accepted"]==true&&cloudChecks==1&&cloudUploadTests==0);
      enterBle();assert(command("CHECK")["accepted"]==false&&cloudChecks==2&&cloudUploadTests==0);
      auto paused=command("BACKUP_STATUS");assert(paused.size()==wanted.size()&&paused["paused"]==true);
      for(auto member:paused.as<JsonObject>())assert(wanted.count(member.key().c_str())==1);
      assert(paused["worker_phase"]==unsigned(BackupWorkerPhase::Stopped));closeBle();
    }else if(scenario=="fixture-config"){
#if CSP_BACKUP_FIXTURE_ONLY
      const std::string wantedURL="https://synthetic.invalid/backup/CSP-INTEGRATION-TEST-20261104/";
      assert(lastStartedConfig.watch_folder=="CSP-INTEGRATION-TEST"&&lastStartedConfig.server_url==wantedURL);
      assert(lastStartedConfig.backup_mode=="idle"&&lastStartedConfig.idle_seconds==30&&lastStartedConfig.sync_interval_s==30);
#else
      const std::string wantedURL="https://synthetic.invalid/backup/";
      assert(lastStartedConfig.watch_folder=="QA"&&lastStartedConfig.server_url==wantedURL);
      assert(lastStartedConfig.idle_seconds==120&&lastStartedConfig.sync_interval_s==60);
#endif
      const auto watch=lastStartedConfig.watch_folder;enterBle();closeBle();
      assert(lastStartedConfig.watch_folder==watch&&lastStartedConfig.server_url==wantedURL);
      assert(testNvs.at("csp/config")==nvsBefore.at("csp/config"));
    }else if(scenario=="backup-status"){
      enterBle();testAuthorized=true;
      observedBackup.lastBackup=253402300799ULL;
      observedBackup.verified=UINT32_MAX;observedBackup.pending=UINT32_MAX;observedBackup.failed=UINT32_MAX;
      observedBackup.error=INT_MIN;
      std::strcpy(observedBackup.currentFile,"private-synthetic-filename.pdf");
      std::strcpy(observedBackup.message,"synthetic-cloud-password must never be returned");
      auto status=application("{\"op\":\"backup_status\"}");
      assert(status.size()==8&&status["ok"]==true&&status["cloud_configured"]==true&&status["backups_paused"]==true);
      assert(status["last_backup_unix"].as<uint64_t>()==253402300799ULL);
      assert(status["verified"].as<uint32_t>()==UINT32_MAX&&status["pending"].as<uint32_t>()==UINT32_MAX&&status["failed"].as<uint32_t>()==UINT32_MAX);
      assert(status["backup_error"].as<int>()==INT_MIN);
      assert(applicationRaw.find("private-synthetic-filename")==std::string::npos);
      assert(applicationRaw.find("synthetic-cloud-password")==std::string::npos);
      const auto plain=applicationPlaintext,envelope=applicationEnvelope;
      auto original=application("{\"op\":\"status\"}");
      assert(original["ok"]==true&&!original["cloud_configured"].is<bool>()&&!original["last_backup_unix"].is<uint64_t>());
      assert(!original["verified"].is<uint32_t>()&&!original["pending"].is<uint32_t>()&&!original["failed"].is<uint32_t>());
      assert(!original["backup_error"].is<int>()&&original["backups_paused"]==true);
      std::cout<<"Backup status maximum fixture: "<<plain<<" plaintext bytes; "<<envelope<<" envelope bytes\n";
      closeBle();
    }else if(scenario=="same-profile-save"){
      enterBle();testAuthorized=true;
      auto begin=application("{\"op\":\"begin\",\"revision\":\"2\"}");assert(begin["ok"]==true);
      const auto tx=begin["transaction"].as<std::string>();
      auto stage=application("{\"op\":\"stage\",\"transaction\":\""+tx+"\",\"profile\":1,\"ssid\":\"Synthetic AP\",\"password\":\"\",\"open\":false,\"keep\":true,\"priority\":0,\"enabled\":true}");assert(stage["ok"]==true);
      auto check=application("{\"op\":\"check\",\"transaction\":\""+tx+"\",\"profile\":1}");assert(check["ok"]==true);tick();
      auto status=application("{\"op\":\"status\"}");assert(status["phase"]==unsigned(pro::wifi_setup::Phase::Ready));
      const auto edit=status["edit"].as<unsigned>();
      auto commit=application("{\"op\":\"commit\",\"transaction\":\""+tx+"\",\"revision\":\"2\",\"edit\":"+std::to_string(edit)+",\"accept_local\":true}");
      assert(commit["ok"]==true&&commit["revision"]=="3");
      pro::wifi_setup::StoredNetworks after;assert(fixture.load(after)==pro::wifi_setup::Stored::Valid);
      assert(after.revision==3&&after.count==1&&after.profiles[0].secret==saved.profiles[0].secret);
      assert(testNvs.at("csp/config")==nvsBefore.at("csp/config"));
      assert(testNvs.at("csp-owner/owner")==nvsBefore.at("csp-owner/owner"));closeBle();
    }else assert(false);
  }
  if(scenario!="same-profile-save")assert(testNvs==nvsBefore&&nvsWrites==writesBefore);
  assert(usbInitializations==1&&usbPolls>0&&testHandles.empty());
  for(const auto& line:USBSerial.lines){assert(line.find("synthetic-cloud-password")==std::string::npos);assert(line.find("synthetic-wifi-password")==std::string::npos);}
  std::cout<<"PASS combined runtime "<<scenario<<"; actual owner loop; simulated hardware only\n";
}
