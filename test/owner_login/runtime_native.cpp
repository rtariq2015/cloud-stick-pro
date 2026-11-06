// Actual owner runtime/mailbox/codec/NVS/config; radio/USB/worker/ACK crypto
// notification are synthetic. Actual SDK cryptography is tested separately.
#include "cloud-runtime-prefix.h"
#if CSP_BACKUP_WIFI_AWAKE
BackupPolicy backup_policy(){return {};}
bool backup_window_open(){return true;}
uint32_t storage_quiet_remaining(uint32_t){return 0;}
#endif
bool testConnected=true;
BleManagementStatus ble_management_status(){BleManagementStatus s;s.active=bleRunning;s.connected=bleRunning&&testConnected;s.authenticated=bleRunning&&testAuthorized;return s;}
std::string hex(const uint8_t* input,size_t size){const char* digits="0123456789abcdef";std::string out;for(size_t i=0;i<size;++i){out+=digits[input[i]>>4];out+=digits[input[i]&15];}return out;}
std::string loginBegin(){
  auto begun=application("{\"op\":\"login_begin\"}");assert(begun["ok"]==true&&begun["error"]==0&&begun["received"]==0&&begun["edit"]==0&&begun["device"]=="001122334455");
  assert(begun["durable"]==false&&begun["reconnect_required"]==false&&begun["store_fault"]==false);return begun["transaction"].as<std::string>();
}
uint32_t loginStage(const std::string& transaction,const std::vector<uint8_t>& bytes){
  uint32_t edit=0,offset=0;while(offset<bytes.size()){
    const size_t count=std::min(size_t(128),bytes.size()-offset);
    auto result=application("{\"op\":\"login_stage\",\"transaction\":\""+transaction+"\",\"edit\":"+std::to_string(edit)+",\"offset\":"+std::to_string(offset)+",\"data\":\""+hex(bytes.data()+offset,count)+"\"}");
    assert(result["ok"]==true&&result["edit"]==edit+1&&result["received"]==offset+count);offset+=count;++edit;
  }return edit;
}
std::string loginCommit(const std::string& transaction,uint64_t revision,uint32_t edit,bool accept=true){return "{\"op\":\"login_commit\",\"transaction\":\""+transaction+"\",\"revision\":\""+std::to_string(revision)+"\",\"edit\":"+std::to_string(edit)+",\"accept\":"+(accept?"true":"false")+"}";}
void deniedOldCommands(){
  for(const auto* operation:{"status","backup_status","history_status","login_begin","cloud_status"}){
    const std::string text=std::string("{\"v\":2,\"id\":900,\"op\":\"")+operation+"\"}";WireRequest parsed;assert(parseWire(reinterpret_cast<const uint8_t*>(text.data()),text.size(),parsed));
    uint8_t* output=nullptr;ssize_t length=0;assert(wifi_setup_endpoint({7,9},parsed,reinterpret_cast<const uint8_t*>(text.data()),text.size(),&output,&length)==ESP_OK);
    JsonDocument result;assert(!deserializeJson(result,output,size_t(length)));std::free(output);assert(result["ok"]==false&&result["error"]=="reconnect_required");
  }
  assert(!wifi_setup_owner_login_info_allowed());WireRequest request;request.op=Op::CloudStatus;JsonDocument result;perform({7,9},request,result);assert(result["error"]==unsigned(Error::Denied));
}
int main(int argc,char** argv){
  assert(argc==4);const std::string scenario=argv[3];
  std::ifstream oldInput(argv[1],std::ios::binary),newInput(argv[2],std::ios::binary);
  std::vector<uint8_t> oldBytes{std::istreambuf_iterator<char>(oldInput),{}},newBytes{std::istreambuf_iterator<char>(newInput),{}};
  assert(oldBytes.size()==432&&newBytes.size()==432&&oldBytes!=newBytes);testNvs["csp-owner/owner"]=oldBytes;
  const auto raw=initialConfig();testNvs["csp/config"]={raw.begin(),raw.end()};
  StoredNetworks networks;networks.revision=2;networks.count=1;networks.selected=1;networks.next_id=2;networks.profiles[0].id=1;
  std::strcpy(networks.profiles[0].ssid.data(),"Current v2 AP");std::strcpy(networks.profiles[0].secret.data(),"synthetic-wifi-password");
  WifiNvsStore fixtureNvs;ProfileStore fixture(fixtureNvs);assert(fixture.publish(networks,1,[](void*){return true;},nullptr)==Published::Committed);
  const auto original=testNvs;const auto beforeWrites=nvsWrites;setup();tick();assert(workerStarts==1&&nvsWrites==beforeWrites);
  enableWindow();protocomm_security2_params_t oldParameters;assert(wifi_setup_owner_parameters(oldParameters));
  std::array<uint8_t,16> oldSalt;std::memcpy(oldSalt.data(),oldParameters.salt,oldSalt.size());
  std::array<uint8_t,384> oldVerifier;std::memcpy(oldVerifier.data(),oldParameters.verifier,oldVerifier.size());
  if(scenario=="wifi-busy"){
    assert(application("{\"op\":\"begin\",\"revision\":\"2\"}")["ok"]==true);
    assert(application("{\"op\":\"login_begin\"}")["error"]==unsigned(Error::Busy));
  }else if(scenario=="cloud-busy"){
    assert(application("{\"op\":\"cloud_begin\",\"revision\":\"1\"}")["ok"]==true);
    assert(application("{\"op\":\"login_begin\"}")["error"]==unsigned(Error::Busy));
  }else{
    const auto transaction=loginBegin();const auto revision=login_draft->base.revision;
    assert(application("{\"op\":\"begin\",\"revision\":\"2\"}")["error"]==unsigned(Error::Busy));
    assert(application("{\"op\":\"cloud_begin\",\"revision\":\"1\"}")["error"]==unsigned(Error::Busy));
    assert(application("{\"op\":\"history_status\"}")["error"]==unsigned(Error::Busy));
    if(scenario=="crc")newBytes[428]^=1;
    if(scenario=="mac")newBytes[6]^=1;
    const auto edit=loginStage(transaction,newBytes);
    if(scenario=="abort"){
      auto aborted=application("{\"op\":\"login_abort\",\"transaction\":\""+transaction+"\"}");assert(aborted["ok"]==true&&aborted["edit"]==0&&aborted["received"]==0&&!login_draft);
    }else if(scenario=="disconnect-stage"){
      testAuthorized=false;tick();assert(!login_draft&&!login_barrier.load());
    }else if(scenario=="crc"||scenario=="mac"||scenario=="no-consent"||scenario=="stale-edit"){
      auto result=application(loginCommit(transaction,revision,scenario=="stale-edit"?edit-1:edit,scenario!="no-consent"));
      assert(result["error"]==unsigned(scenario=="no-consent"?Error::ConfirmationRequired:scenario=="stale-edit"?Error::StaleTransaction:Error::Invalid));
      assert(!login_barrier.load()&&nvsWrites==beforeWrites&&testNvs==original);
    }else if(scenario=="window-expiry"){
      testNow+=pro::BleLifecycle::WindowMs;WireRequest request;
      const std::string text="{\"v\":2,\"id\":901,"+loginCommit(transaction,revision,edit).substr(1);assert(parseWire(reinterpret_cast<const uint8_t*>(text.data()),text.size(),request));
      JsonDocument result;perform({7,9},request,result);assert(result["error"]==unsigned(Error::Denied)&&!login_barrier.load()&&nvsWrites==beforeWrites);
    }else if(scenario=="cas"){
      testNvs["csp-owner/owner"]=newBytes;assert(application(loginCommit(transaction,revision,edit))["error"]==unsigned(Error::StaleRevision));assert(!login_barrier.load()&&nvsWrites==beforeWrites);
    }else{
      if(scenario=="unknown")loseAck=true;
      auto committed=application(loginCommit(transaction,revision,edit));const auto commitReply=applicationRaw;
      assert(login_barrier.load()&&!login_draft&&maintenance&&managed.phase==BackupWorkerPhase::Stopped);
      assert(std::memcmp(oldParameters.salt,oldSalt.data(),oldSalt.size())==0&&std::memcmp(oldParameters.verifier,oldVerifier.data(),oldVerifier.size())==0);
      if(scenario=="unknown")assert(committed["ok"]==false&&committed["store_fault"]==true&&login_fault&&backup_bridge_fault);
      else assert(committed["ok"]==true&&committed["error"]==0&&committed["durable"]==true&&committed["reconnect_required"]==true&&committed["revision"].as<std::string>()!=std::to_string(revision));
      const auto writes=nvsWrites;deniedOldCommands();assert(nvsWrites==writes);
      uint32_t ticket=0;
      assert(wifi_setup_owner_login_output_allowed({7,9},reinterpret_cast<const uint8_t*>(commitReply.data()),commitReply.size(),ticket)&&ticket==mailbox.job);
      assert(!wifi_setup_owner_login_output_allowed({7,10},reinterpret_cast<const uint8_t*>(commitReply.data()),commitReply.size(),ticket));
      const std::string sensitive="{\"v\":1,\"id\":9,\"ok\":true,\"device_id\":\"private\"}";
      assert(!wifi_setup_owner_login_output_allowed({7,9},reinterpret_cast<const uint8_t*>(sensitive.data()),sensitive.size(),ticket));
      auto corrupt=commitReply;corrupt.back()='x';assert(!wifi_setup_owner_login_output_allowed({7,9},reinterpret_cast<const uint8_t*>(corrupt.data()),corrupt.size(),ticket));
      if(scenario=="late-job"){
        const std::string stale="{\"v\":2,\"id\":902,\"op\":\"cloud_begin\",\"revision\":\"1\"}";
        {std::lock_guard<std::mutex> lock(mailbox_mutex);mailbox.job+=1;mailbox.state=Mailbox::State::Queued;mailbox.input_size=stale.size();std::memcpy(mailbox.input.data(),stale.data(),stale.size());}
        worker();assert(mailbox.state==Mailbox::State::Empty&&!cloud_draft&&nvsWrites==writes);
      }else{
        const auto starts=workerStarts;
        if(scenario!="no-ack"&&scenario!="disconnect-commit")wifi_setup_owner_login_ack_encrypted({7,9},mailbox.job);
        if(scenario=="cleanup-fail")simulatedStopOk=false;
        if(scenario=="reload-fail")testNvs["csp-owner/owner"][0]^=1;
        if(scenario=="disconnect-commit")testConnected=false;
        if(scenario=="no-ack"){tick(4990);assert(bleRunning&&workerStarts==starts);tick(10);}else if(scenario=="disconnect-commit")tick();else{tick(999);assert(bleRunning);tick(1);}
        tick(501);
        if(scenario=="unknown"||scenario=="cleanup-fail"||scenario=="reload-fail"){
          assert(maintenance&&workerStarts==starts&&login_barrier.load());
          assert(std::memcmp(oldParameters.salt,oldSalt.data(),oldSalt.size())==0);
        }else{
          assert(!bleRunning&&!login_barrier.load()&&!login_fault&&!maintenance&&workerStarts==starts+1);
          assert(owner.salt!=oldSalt&&std::equal(owner.salt.begin(),owner.salt.end(),newBytes.begin()+12));
          protocomm_security2_params_t next;assert(wifi_setup_owner_parameters(next));assert(std::memcmp(next.verifier,newBytes.data()+28,384)==0);
        }
      }
    }
  }
  for(const auto& item:original)if(item.first!="csp-owner/owner")assert(testNvs[item.first]==item.second);
  assert(usbInitializations==1&&testHandles.empty());
#if CSP_BACKUP_WIFI_AWAKE
  assert(integrated_dashboard_fixture::polls>0);
  assert(!bleRunning||(!backup_power.owned()&&!integrated_dashboard_fixture::open));
  if(!maintenance&&managed.phase==BackupWorkerPhase::Running)assert(backup_power.owned());
#endif
  clearLogin();clearCloud();
  std::cout<<"PASS actual owner-login runtime "<<scenario<<" sizes="<<sizeof(LoginDraft)<<","<<sizeof(login_handoff)<<"; no hardware\n";
}
