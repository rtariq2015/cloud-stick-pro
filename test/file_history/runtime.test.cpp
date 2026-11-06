// Actual owner/runtime/Journal/history/parser, synthetic SDK/NVS/USB/worker only.
#if CSP_ENABLE_FILE_HISTORY
#define probeStart history_fixture_probeStart
#endif
#include "public_runtime_fixture_prefix.h"
#undef probeStart
#include "file_history.h"
#include "pro_journal.h"
#include <filesystem>
#include <cstdio>
#include <limits>
namespace fs=std::filesystem;
static bool forbidFileIO=false;static unsigned fileOpens=0;
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){assert(!forbidFileIO);++fileOpens;return __real_fopen(path,mode);}
static unsigned cases=0;static size_t maximumPlain=0,maximumEnvelope=0;
static void pass(const char* what){++cases;std::cout<<"PASS "<<what<<'\n';}
#if CSP_ENABLE_FILE_HISTORY
namespace pro::public_wifi {
bool probeStart(){
 assert(!bleRunning&&maintenance&&managed.phase==BackupWorkerPhase::Stopped);
 assert(pro::history::cacheBytesForTest()==0&&!pro::history::status().loaded);
 return history_fixture_probeStart();
}
}
#endif
static void deny(WifiSetupSession s={7,9}){
 const std::string raw="{\"v\":2,\"id\":1,\"op\":\"history_status\"}";pro::wifi_setup::WireRequest r;r.id=1;
#if CSP_ENABLE_FILE_HISTORY
 r.op=pro::wifi_setup::Op::HistoryStatus;
#endif
 uint8_t* out=reinterpret_cast<uint8_t*>(uintptr_t(1));ssize_t n=-1;
 assert(wifi_setup_endpoint(s,r,reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),&out,&n)==ESP_ERR_INVALID_STATE);
 assert(out==nullptr&&n==0);
}
int main(int argc,char** argv){
 assert(argc==3);fixture(argv[1]);const auto before=testNvs;const auto writes=nvsWrites,polls=usbPolls;
 testAuthorized=false;deny();testAuthorized=true;deny({8,9});deny({7,10});pass("private owner/stale session admission denies without output");
#if CSP_ENABLE_FILE_HISTORY
 auto size=[&]{maximumPlain=std::max(maximumPlain,applicationPlaintext);maximumEnvelope=std::max(maximumEnvelope,applicationEnvelope);assert(applicationPlaintext<400&&applicationEnvelope<496);};
 auto stat=[&]{auto d=application("{\"op\":\"history_status\"}");size();return d;};
 pro::history::release();forbidFileIO=true;auto unloaded=stat();assert(unloaded["loaded"]==false&&unloaded["n"]==0);forbidFileIO=false;
 fs::path root=argv[2];fs::remove_all(root);fs::create_directories(root);pro::FileJournal journal(root.string());
 const std::string dest(64,'a');
 for(unsigned i=0;i<8;++i){
  pro::Record record;record.path="QA/"+std::string(237,'\"');record.path[3]=char('a'+i);record.destination=dest;
  record.digest=std::string(64,'b');record.sequence=1;record.state=pro::State(i%3);record.size=UINT64_MAX;
  record.verifiedAt=record.state==pro::State::Verified?UINT64_MAX:0;record.next=UINT64_MAX;
  record.attempts=UINT32_MAX;record.error=INT32_MIN;std::string key(64,'0');key.back()=char('0'+i);assert(journal.save(key,record));
 }
 assert(pro::history::capture(journal,dest,"QA",UINT64_MAX));const auto frozen=pro::history::status();
 const auto opens=fileOpens;forbidFileIO=true;auto status=stat();assert(status["ok"]==true&&status["n"]==8&&status["matched"]==8&&status["complete"]==true);
 assert(status["when"]=="18446744073709551615");
 const auto g=status["g"].as<uint32_t>();
 auto row=[&](uint32_t generation,uint32_t index,uint32_t offset){
  auto d=application("{\"op\":\"history_row\",\"g\":"+std::to_string(generation)+",\"i\":"+std::to_string(index)+",\"o\":"+std::to_string(offset)+"}");size();return d;
 };
 for(unsigned i=0;i<8;++i){
  unsigned offset=0;std::string path;
  for(unsigned fragment=0;fragment<4;++fragment){
   auto data=row(g,i,offset);assert(data["ok"]==true&&data["b"]=="18446744073709551615"&&data["a"]==UINT32_MAX&&data["e"]==INT32_MIN);
   const auto hex=data["h"].as<std::string>();for(size_t j=0;j<hex.size();j+=2)path+=char(std::stoul(hex.substr(j,2),nullptr,16));offset=data["x"].as<unsigned>();
   if(data["m"]==false)break;
  }
  assert(path.size()==240&&offset==240&&path.substr(0,3)=="QA/");
 }
 assert(fileOpens==opens&&pro::history::status().revision==frozen.revision);pass("eight actual persisted rows and 32 fragments fit real 400/496-byte budgets with no BLE filesystem I/O");
 auto stale=row(g+1,0,0);assert(stale["ok"]==false&&stale["error"]==unsigned(pro::wifi_setup::Error::StaleRevision));
 auto badOffset=row(g,0,241-1);assert(badOffset["ok"]==true&&badOffset["h"]=="");
 maintenance=false;auto busy=stat();assert(busy["ok"]==false&&busy["error"]==unsigned(pro::wifi_setup::Error::Busy));maintenance=true;
 for(auto phase:{BackupWorkerPhase::Starting,BackupWorkerPhase::Running,BackupWorkerPhase::Retiring,BackupWorkerPhase::Drained,BackupWorkerPhase::Failed}){
  managed.phase=phase;busy=stat();assert(busy["ok"]==false&&busy["error"]==unsigned(pro::wifi_setup::Error::Busy));
 }
 managed.phase=BackupWorkerPhase::Stopped;pass("revision/offset validation and maintenance/worker exclusion preserve frozen snapshot");
 const std::string raw="{\"v\":2,\"id\":2147483646,\"op\":\"history_status\"}";pro::wifi_setup::WireRequest r;
 assert(pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),r));uint8_t* out=nullptr;ssize_t n=0;
 assert(wifi_setup_endpoint({7,9},r,reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),&out,&n)==ESP_OK);
 JsonDocument queued;assert(!deserializeJson(queued,out,size_t(n)));std::free(out);const auto job=queued["job"].as<uint32_t>();
 testAuthorized=false;tick();testAuthorized=true;
 const std::string poll="{\"v\":2,\"id\":2147483647,\"op\":\"result\",\"job\":"+std::to_string(job)+"}";
 assert(pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(poll.data()),poll.size(),r));
 assert(wifi_setup_endpoint({7,9},r,reinterpret_cast<const uint8_t*>(poll.data()),poll.size(),&out,&n)==ESP_OK);
 JsonDocument revoked;assert(!deserializeJson(revoked,out,size_t(n)));std::free(out);assert(revoked["ok"]==false&&revoked["error"]=="stale_job"&&revoked["data"].isNull());
 pass("revoked queued history cannot be retrieved after authentication returns");
 assert(fileOpens==opens&&pro::history::cacheBytesForTest()==2240);
 auto prepared=beginPortal();assert(prepared["ok"]==true&&bleRunning&&pro::history::cacheBytesForTest()==2240);
 const auto grant=prepared["grant"].as<std::string>();
 auto ack=application("{\"op\":\"portal_handoff\",\"profile\":1,\"revision\":\"2\",\"grant\":\""+grant+"\"}");
 assert(ack["ok"]==true&&ack["phase"]=="disconnect_to_continue"&&bleRunning&&pro::history::cacheBytesForTest()==2240);
 tick();assert(bleRunning&&routeStarts==0&&probeStarts==0&&pro::history::cacheBytesForTest()==2240);
 publicConnected=false;testAuthorized=false;waitRoute();
 assert(!bleRunning&&maintenance&&routeStarts==1&&probeStarts==0&&pro::history::cacheBytesForTest()==0);
 assert(!pro::history::status().loaded);tick(300001);tick();tick();
 assert(probeStarts==1&&probeState.phase==pro::public_wifi::ProbePhase::Running&&pro::history::cacheBytesForTest()==0);
 probeState={pro::public_wifi::ProbePhase::Drained,true,207};tick();tick();
 assert(!maintenance&&probeReaps==1&&fileOpens==opens);
 pass("authenticated handoff retains history until BLE cleanup, then frees it before route and independent TLS-probe admission");
 forbidFileIO=false;pro::history::release();assert(pro::history::cacheBytesForTest()==0);
#else
 pro::wifi_setup::WireRequest r;const std::string raw="{\"v\":2,\"id\":1,\"op\":\"history_status\"}";
 assert(!pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),r));assert(application("{\"op\":\"status\"}")["ok"]==true);
 pass("default-off runtime refuses history operation and preserves legacy status");
#endif
 assert(testNvs==before&&nvsWrites==writes&&usbInitializations==1&&usbPolls>polls&&cloudChecks==0&&cloudUploadTests==0);
 for(const auto& line:USBSerial.lines)for(const char* secret:{"synthetic-cloud-password","synthetic-wifi-password","fixture-owner","QA/"})assert(line.find(secret)==std::string::npos);
 pass("settings, USB enumeration and callback flow unchanged; no private strings logged");
 std::cout<<cases<<" actual file history runtime cases passed; max_plain="<<maximumPlain<<" max_envelope="<<maximumEnvelope<<'\n';
}
