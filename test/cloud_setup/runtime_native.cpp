#define load_backup_config fixtureLegacyBackupConfig
#include "runtime-fixture-prefix.h"
#undef load_backup_config
#include "koofr_ca.h"
#include "file_history.h"
namespace pro::history {
Status fixtureHistory;unsigned releases=0,reads=0;
void release(){++releases;fixtureHistory=Status{};}
Status status(){return fixtureHistory;}
Read read(uint32_t,uint32_t,uint32_t,Fragment& out){++reads;out=Fragment{};return Read::Unavailable;}
}
CloudConfig load_backup_config();
#include "../../src/wifi_setup_runtime.cpp"
unsigned cloudInvalidations=0;
bool backup_invalidate_cloud_status(){
  assert(managed.phase==BackupWorkerPhase::Stopped&&maintenance);
  ++cloudInvalidations;observedBackup=BackupStatus{};return true;
}
std::string initialConfig(){
  JsonDocument doc;doc["schema"]=1;doc["mode"]="upload_only";
  doc["server_url"]=KoofrServer;doc["username"]="synthetic-old-user";doc["password"]="synthetic-old-password";
  doc["ca_pem"]=KoofrCA;doc["ntp_server"]="pool.ntp.org";
  doc["ssid"]="Legacy saved SSID";doc["wifi_password"]="legacy-synthetic-password";doc["wifi_open"]=false;
  doc["watch_folder"]="QA";doc["backup_mode"]="idle";doc["idle_seconds"]=120;
  doc["unknown_preserve"]["value"]=17;
  std::string out;serializeJson(doc,out);return out;
}
void enableWindow(){
  openWindow();if(!bleRunning){drained();tick();}
  assert(bleRunning&&maintenance&&managed.phase==BackupWorkerPhase::Stopped);testAuthorized=true;
}
std::string beginCloud(){
  auto status=application("{\"op\":\"cloud_status\"}");
  assert(status["ok"]==true&&status["revision"]=="1"&&status["store_fault"]==false);
  auto begun=application("{\"op\":\"cloud_begin\",\"revision\":\"1\"}");assert(begun["ok"]==true&&begun["edit"]==0);
  return begun["transaction"].as<std::string>();
}
void stageCloud(const std::string& transaction,bool all=true){
  unsigned edit=0;
  if(all)for(const auto& field:std::vector<std::pair<std::string,std::string>>{{"username","synthetic-new-user@example.invalid"},{"app_password","synthetic-new-app-password"}}){
    auto r=application("{\"op\":\"cloud_stage\",\"transaction\":\""+transaction+"\",\"edit\":"+std::to_string(edit)+",\"field\":\""+field.first+"\",\"value\":\""+field.second+"\"}");
    assert(r["ok"]==true&&r["edit"]==++edit);
  }
  auto r=application("{\"op\":\"cloud_stage\",\"transaction\":\""+transaction+"\",\"edit\":"+std::to_string(edit)+",\"field\":\"folder\",\"value\":\"SyntheticDeviceQA\"}");
  assert(r["ok"]==true&&r["edit"]==edit+1);
}
std::string commitRequest(const std::string& transaction,unsigned edit=3,bool accept=true){
  return "{\"op\":\"cloud_commit\",\"transaction\":\""+transaction+"\",\"revision\":\"1\",\"edit\":"+std::to_string(edit)+",\"accept_unverified\":"+(accept?"true":"false")+"}";
}
void assertNoSecrets(){
  for(const auto& line:USBSerial.lines)for(const auto* marker:{"synthetic-new-user","synthetic-new-app-password","synthetic-old-user","synthetic-old-password","legacy-synthetic-password","Legacy saved SSID","SyntheticDeviceQA"})assert(line.find(marker)==std::string::npos);
  for(const auto* marker:{"synthetic-new-user","synthetic-new-app-password","synthetic-old-user","synthetic-old-password","server_url","ca_pem","Legacy saved SSID"})assert(applicationRaw.find(marker)==std::string::npos);
}
int main(int argc,char** argv){
  assert(argc==3);const std::string scenario=argv[2];
  std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> ownerBytes{std::istreambuf_iterator<char>(input),{}};
  assert(ownerBytes.size()==432);testNvs["csp-owner/owner"]=ownerBytes;
  if(scenario!="new-board"&&scenario!="no-wifi"){
    const auto raw=initialConfig();testNvs["csp/config"]={raw.begin(),raw.end()};
  }
  if(scenario!="no-wifi"){
    StoredNetworks saved;saved.revision=2;saved.count=1;saved.selected=1;saved.next_id=2;
    saved.profiles[0].id=1;std::strcpy(saved.profiles[0].ssid.data(),"Current v2 AP");std::strcpy(saved.profiles[0].secret.data(),"synthetic-wifi-password");
    WifiNvsStore fixtureNvs;ProfileStore fixture(fixtureNvs);assert(fixture.publish(saved,1,[](void*){return true;},nullptr)==Published::Committed);
  }
  const auto initial=testNvs;const auto initialWrites=nvsWrites;
  setup();tick();assert(usbInitializations==1&&nvsWrites==initialWrites);
  if(scenario=="new-board"||scenario=="no-wifi")assert(!backup_config_valid&&workerStarts==0&&clockCalls==0);
  else assert(backup_config_valid&&workerStarts==1&&clockCalls==1);
  enableWindow();
  if(scenario=="heap-low"||scenario=="largest-low"){
    if(scenario=="heap-low")runtimeFreeHeap=cloud::OperationFreeFloor-1;else testLargestBlock=cloud::OperationLargestFloor-1;
    if(scenario=="largest-low"){
      // Below16KiB also triggers the existing BLE close policy at the next
      // poll. Check this command's own barrier before polling, rather than
      // incorrectly expecting a GATT result after that deliberate teardown.
      const std::string raw="{\"v\":2,\"id\":1,\"op\":\"cloud_begin\",\"revision\":\"1\"}";
      WireRequest request;assert(parseWire(reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),request));
      JsonDocument result;perform({7,9},request,result);assert(result["error"]==unsigned(Error::Unavailable));
      tick();assert(!bleRunning&&maintenance);
    }else assert(application("{\"op\":\"cloud_begin\",\"revision\":\"1\"}")["error"]==unsigned(Error::Unavailable));
    assert(!cloud_draft&&testNvs==initial&&nvsWrites==initialWrites);
  }else if(scenario=="unauthenticated"){
    testAuthorized=false;
    const std::string raw="{\"v\":2,\"id\":1,\"op\":\"cloud_status\"}";WireRequest request;assert(parseWire(reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),request));
    uint8_t* out=nullptr;ssize_t size=0;assert(wifi_setup_endpoint({7,9},request,reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),&out,&size)==ESP_ERR_INVALID_STATE);
    assert(!out&&size==0&&testNvs==initial&&nvsWrites==initialWrites);
  }else if(scenario=="wifi-mutual-exclusion"){
    auto wifi=application("{\"op\":\"begin\",\"revision\":\"2\"}");assert(wifi["ok"]==true);
    assert(application("{\"op\":\"cloud_begin\",\"revision\":\"1\"}")["error"]==unsigned(Error::Busy));
    assert(!cloud_draft&&testNvs==initial&&nvsWrites==initialWrites);
  }else{
    observedBackup.verified=4;observedBackup.hasScan=true;observedBackup.scanComplete=true;
    pro::history::fixtureHistory.loaded=true;pro::history::fixtureHistory.count=1;
    assert(application("{\"op\":\"history_status\"}")["loaded"]==true);
    const auto transaction=beginCloud();
    const auto historyReads=pro::history::reads;
    assert(application("{\"op\":\"history_status\"}")["error"]==unsigned(Error::Busy));
    assert(application("{\"op\":\"history_row\",\"g\":1,\"i\":0,\"o\":0}")["error"]==unsigned(Error::Busy));
    assert(pro::history::reads==historyReads);
    assert(application("{\"op\":\"begin\",\"revision\":\"2\"}")["error"]==unsigned(Error::Busy));
    stageCloud(transaction,scenario!="keep-secrets");
    const unsigned edit=scenario=="keep-secrets"?1:3;
    if(scenario=="disconnect"||scenario=="auth-loss"){
      if(scenario=="auth-loss"){testAuthorized=false;tick();assert(!cloud_draft);}
      closeBle();assert(!cloud_draft&&testNvs==initial&&nvsWrites==initialWrites&&cloudInvalidations==0);
      assert(lastStartedConfig.server_url==KoofrServer);
    }else if(scenario=="abort"){
      assert(application("{\"op\":\"cloud_abort\",\"transaction\":\""+transaction+"\"}")["ok"]==true);
      assert(!cloud_draft&&testNvs==initial&&nvsWrites==initialWrites&&cloudInvalidations==0);
    }else if(scenario=="no-consent"){
      assert(application(commitRequest(transaction,edit,false))["error"]==unsigned(Error::ConfirmationRequired));assert(testNvs==initial&&nvsWrites==initialWrites);
    }else if(scenario=="no-wifi"){
      assert(application(commitRequest(transaction))["error"]==unsigned(Error::CheckRequired));assert(testNvs==initial&&nvsWrites==initialWrites);
    }else if(scenario=="stale-edit"){
      assert(application(commitRequest(transaction,edit-1))["error"]==unsigned(Error::StaleTransaction));assert(testNvs==initial&&nvsWrites==initialWrites);
    }else if(scenario=="physical-expiry"){
      // Do not poll the lifecycle first: the command/write barrier itself must
      // reject a deadline crossed after the last owner-loop observation.
      testNow+=pro::BleLifecycle::WindowMs;
      const auto text="{\"v\":2,\"id\":91,"+commitRequest(transaction).substr(1);WireRequest request;
      assert(parseWire(reinterpret_cast<const uint8_t*>(text.data()),text.size(),request));JsonDocument result;
      perform({7,9},request,result);assert(result["error"]==unsigned(Error::Denied)&&testNvs==initial&&nvsWrites==initialWrites);
    }else if(scenario=="raw-conflict"){
      JsonDocument changed;assert(!deserializeJson(changed,testNvs["csp/config"]));changed["unknown_preserve"]["value"]=99;
      std::string raw;serializeJson(changed,raw);testNvs["csp/config"]={raw.begin(),raw.end()};const auto edited=testNvs;
      assert(application(commitRequest(transaction))["error"]==unsigned(Error::StaleRevision));assert(testNvs==edited&&nvsWrites==initialWrites);
    }else{
      if(scenario=="unknown-commit")loseAck=true;
      auto result=application(commitRequest(transaction,edit));
      if(scenario=="unknown-commit"){
        assert(result["ok"]==false&&result["error"]==unsigned(Error::Unavailable)&&result["store_fault"]==true);
        assert(!cloud_draft&&cloud_store_fault&&backup_bridge_fault&&maintenance&&cloudInvalidations==0);
        const auto writes=nvsWrites,reads=nextHandle;auto uncertain=application("{\"op\":\"cloud_status\"}");
        assert(uncertain["ok"]==true&&uncertain["store_fault"]==true&&uncertain["configured"]==false&&nvsWrites==writes&&nextHandle==reads);
        assert(application("{\"op\":\"cloud_begin\",\"revision\":\"1\"}")["store_fault"]==true&&nvsWrites==writes);
        const auto starts=workerStarts;assert(command("BENCH_CLOSE")["bench_close"]==true);tick(60000);
        assert(!bleRunning&&maintenance&&workerStarts==starts&&backup_bridge_fault);
      }else{
        assert(result["ok"]==true&&result["durable"]==true&&result["revision"]=="2"&&result["access_verified"]==false);
        assert(!cloud_draft&&!cloud_store_fault&&backup_config_valid&&cloud_clock_pending&&cloudInvalidations==1);
        assert(observedBackup.verified==0&&!observedBackup.hasScan&&observedBackup.lastBackup==0);
        assert(application("{\"op\":\"history_status\"}")["loaded"]==false&&pro::history::releases>0);
        JsonDocument stored;assert(!deserializeJson(stored,testNvs["csp/config"]));
        assert(stored["server_url"]=="https://app.koofr.net/dav/Koofr/SyntheticDeviceQA/"&&stored["cloud_rev"]==2);
        if(scenario=="keep-secrets")assert(stored["username"]=="synthetic-old-user"&&stored["password"]=="synthetic-old-password");
        else assert(stored["username"]=="synthetic-new-user@example.invalid"&&stored["password"]=="synthetic-new-app-password");
        if(scenario!="new-board")assert(stored["unknown_preserve"]["value"]==17&&stored["ssid"]=="Legacy saved SSID"&&stored["wifi_password"]=="legacy-synthetic-password");
        else assert(stored["ssid"]=="Current v2 AP"&&stored["wifi_password"]=="synthetic-wifi-password");
        const auto clocks=clockCalls;
        auto status=application("{\"op\":\"cloud_status\"}");assert(status["configured"]==true&&status["revision"]=="2"&&status["has_password"]==true);
        assert(clockCalls==clocks);assertNoSecrets();closeBle();
        assert(!cloud_clock_pending&&clockCalls==clocks+1&&managed.phase==BackupWorkerPhase::Running);
        assert(lastStartedConfig.server_url=="https://app.koofr.net/dav/Koofr/SyntheticDeviceQA/");
      }
    }
  }
  assert(testNvs["csp-owner/owner"]==initial.at("csp-owner/owner"));
  for(const auto& item:initial)if(item.first!="csp/config")assert(testNvs[item.first]==item.second);
  assert(usbInitializations==1&&testHandles.empty());assertNoSecrets();clearCloud();
  std::cout<<"Native64 sizes: CloudDraft="<<sizeof(CloudDraft)<<" Record="<<sizeof(cloud::Record)
    <<" Values="<<sizeof(cloud::Values)<<" WireRequest="<<sizeof(WireRequest)<<" StoredNetworks="<<sizeof(StoredNetworks)<<"\n";
  std::cout<<"PASS actual owner-cloud runtime "<<scenario<<"; real codec/NVS/config adapter, synthetic hardware only\n";
}
