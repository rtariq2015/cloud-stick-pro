// The prefix is generated from existing public and combined runtime fixtures.
// It includes the actual main.cpp; the real runtime, parser, NVS profile store
// and logger are separately linked. Only SDK/radio/worker operations are fakes.
#include "public_runtime_fixture_prefix.h"
#include "pro_event_log.h"
#include "io.h"
#include <filesystem>
#include <limits>

namespace fs=std::filesystem;
static const fs::path eventRoot=CSP_EVENT_LOG_ROOT;
static unsigned cases=0;
static void passed(const char* name){++cases;std::cout<<"PASS "<<name<<'\n';}
#if CSP_ENABLE_EVENT_LOG
static size_t maximumPlaintext=0,maximumEnvelope=0;
static std::string request(uint32_t cursor){return "{\"op\":\"event_log\",\"cursor\":"+std::to_string(cursor)+"}";}
#endif
static void deniedAdmission(WifiSetupSession session={7,9}){
  const std::string text="{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":0}";
  pro::wifi_setup::WireRequest parsed;parsed.id=1;
#if CSP_ENABLE_EVENT_LOG
  parsed.op=pro::wifi_setup::Op::EventLog;
#endif
  uint8_t* output=reinterpret_cast<uint8_t*>(uintptr_t(1));ssize_t length=-1;
  assert(wifi_setup_endpoint(session,parsed,reinterpret_cast<const uint8_t*>(text.data()),text.size(),&output,&length)==ESP_ERR_INVALID_STATE);
  assert(output==nullptr&&length==0);
}
#if CSP_ENABLE_EVENT_LOG
static void exactKeys(JsonObjectConst object,std::initializer_list<const char*> fields){
  assert(object.size()==fields.size());
  for(const auto* field:fields)assert(object[field].isNull()==false);
}
static void safeResponse(const JsonDocument& doc){
  exactKeys(doc.as<JsonObjectConst>(),{"ok","events","next","more","newest","persisted","loaded","dirty","corrupt","io_failed","exhausted"});
  assert(doc["ok"]==true&&doc["events"].is<JsonArrayConst>());
  for(auto value:doc["events"].as<JsonArrayConst>())exactKeys(value.as<JsonObjectConst>(),{"s","u","t","k","c"});
  assert(applicationPlaintext<400&&applicationEnvelope<pro::ble::ResponseLimit);
  maximumPlaintext=std::max(maximumPlaintext,applicationPlaintext);
  maximumEnvelope=std::max(maximumEnvelope,applicationEnvelope);
  for(const char* forbidden:{"synthetic-cloud-password","synthetic-wifi-password","fixture-owner","Synthetic AP","synthetic-ca","private_fixture","bytes","path","filename","ssid","password"})
    assert(applicationRaw.find(forbidden)==std::string::npos);
}
static uint32_t crc(const uint8_t* data,size_t size,uint32_t value=0xffffffffu){
  for(size_t i=0;i<size;++i){value^=data[i];for(unsigned j=0;j<8;++j)value=(value>>1)^(0xedb88320u&uint32_t(0-int32_t(value&1)));}return value;
}
static void put32(uint8_t* out,uint32_t value){for(unsigned i=0;i<4;++i)out[i]=uint8_t(value>>(8*i));}
static void resetHistory(){
  pro::events::resetForTest();event_io::clear();fs::remove_all(eventRoot);fs::create_directories(eventRoot);
}
static void untouched(const std::map<std::string,std::vector<uint8_t>>& before,unsigned writes,unsigned opens,unsigned eventWrites,unsigned renames){
  assert(testNvs==before&&nvsWrites==writes&&usbInitializations==1);
  assert(event_io::opens==opens&&event_io::writes==eventWrites&&event_io::renames==renames&&event_io::paths.empty());
  assert(cloudChecks==0&&cloudUploadTests==0);
}
#endif
int main(int argc,char** argv){
  assert(argc==2);fixture(argv[1]);
  const auto nvsBefore=testNvs;const auto writesBefore=nvsWrites,usbBefore=usbPolls;
  testAuthorized=false;deniedAdmission();testAuthorized=true;
  deniedAdmission({8,9});deniedAdmission({7,10});passed("unauthenticated and stale-session endpoint admission denies without output");
#if CSP_ENABLE_EVENT_LOG
  resetHistory();auto unloaded=application(request(0));safeResponse(unloaded);
  assert(unloaded["loaded"]==false&&unloaded["dirty"]==false&&unloaded["events"].size()==0&&event_io::opens==0);
  assert(pro::events::workerLoad()==pro::events::Load::Missing);
  const auto opens=event_io::opens,eventWrites=event_io::writes,renames=event_io::renames;
  auto empty=application(request(0));safeResponse(empty);
  assert(empty["events"].size()==0&&empty["next"]==0&&empty["newest"]==0&&empty["persisted"]==0&&!empty["more"].as<bool>());
  auto future=application(request(1));assert(future["ok"]==false&&future["error"]==unsigned(pro::wifi_setup::Error::Invalid));
  untouched(nvsBefore,writesBefore,opens,eventWrites,renames);passed("empty RAM history and future cursor; export performs no filesystem/NVS writes or reads");

  for(unsigned i=0;i<12;++i)assert(pro::events::record(pro::events::Kind::WorkerStart,INT32_MIN,UINT64_MAX,253402300799ULL,UINT32_MAX));
  uint32_t cursor=0;
  for(unsigned page=0;page<12;++page){
    auto data=application(request(cursor));safeResponse(data);assert(data["events"].size()==1);
    const auto row=data["events"][0];assert(row["s"]==page+1&&row["u"]==UINT32_MAX&&row["t"]==253402300799ULL&&row["c"]==INT32_MIN);
    assert(data["next"]==page+1&&data["newest"]==12&&data["persisted"]==0&&data["dirty"]==true);
    assert(data["more"].as<bool>()==(page<11));cursor=data["next"].as<uint32_t>();
  }
  auto end=application(request(cursor));safeResponse(end);assert(end["events"].size()==0&&end["next"]==cursor&&end["more"]==false);
  auto ahead=application(request(13));assert(ahead["ok"]==false&&ahead["error"]==unsigned(pro::wifi_setup::Error::Invalid));
  assert(pro::events::record(pro::events::Kind::WorkerStart,0,0,0,0));
  auto stale=application(request(0));safeResponse(stale);assert(stale["events"][0]["s"]==2&&stale["next"]==2&&stale["more"]==true);
  untouched(nvsBefore,writesBefore,opens,eventWrites,renames);passed("all 12 events export exactly once, stale/end/future cursors and extreme fields fit under 400 bytes");

  maintenance=false;auto busy=application(request(0));assert(busy["ok"]==false&&busy["error"]==unsigned(pro::wifi_setup::Error::Busy));maintenance=true;
  for(auto phase:{BackupWorkerPhase::Starting,BackupWorkerPhase::Running,BackupWorkerPhase::Retiring,BackupWorkerPhase::Drained,BackupWorkerPhase::Failed}){
    managed.phase=phase;busy=application(request(0));assert(busy["ok"]==false&&busy["error"]==unsigned(pro::wifi_setup::Error::Busy));
  }
  managed.phase=BackupWorkerPhase::Stopped;
  untouched(nvsBefore,writesBefore,opens,eventWrites,renames);passed("maintenance and every nonstopped worker phase deny export");

  // Queue under a valid session, revoke before owner-loop dispatch. The real
  // reapMailbox must erase it; restoring authorization cannot read an old job.
  const std::string text="{\"v\":2,\"id\":2147483646,\"op\":\"event_log\",\"cursor\":0}";
  pro::wifi_setup::WireRequest parsed;assert(pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(text.data()),text.size(),parsed));
  uint8_t* out=nullptr;ssize_t length=0;const WifiSetupSession session{7,9};
  assert(wifi_setup_endpoint(session,parsed,reinterpret_cast<const uint8_t*>(text.data()),text.size(),&out,&length)==ESP_OK);
  JsonDocument queued;assert(!deserializeJson(queued,out,size_t(length)));std::free(out);const auto job=queued["job"].as<uint32_t>();assert(job);
  testAuthorized=false;tick();deniedAdmission();testAuthorized=true;
  const std::string poll="{\"v\":2,\"id\":2147483647,\"op\":\"result\",\"job\":"+std::to_string(job)+"}";
  assert(pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(poll.data()),poll.size(),parsed));
  assert(wifi_setup_endpoint(session,parsed,reinterpret_cast<const uint8_t*>(poll.data()),poll.size(),&out,&length)==ESP_OK);
  JsonDocument revoked;assert(!deserializeJson(revoked,out,size_t(length)));std::free(out);assert(revoked["ok"]==false&&revoked["error"]=="stale_job"&&revoked["data"].isNull());
  untouched(nvsBefore,writesBefore,opens,eventWrites,renames);passed("queued export revoked before dispatch is erased and cannot be retrieved after authorization returns");

  resetHistory();assert(pro::events::workerLoad()==pro::events::Load::Missing);
  assert(pro::events::record(pro::events::Kind::Error,INT32_MIN,UINT64_MAX,253402300799ULL,UINT32_MAX));
  assert(pro::events::workerFlush()==pro::events::Flush::Saved);
  std::ifstream source(eventRoot/"events.log",std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(source),{}};source.close();assert(bytes.size()==52);
  // Edit a synthetic on-disk valid snapshot, then use the actual bounded loader.
  // No test-only mutation of runtime/logger internals is used.
  put32(bytes.data()+8,UINT32_MAX);put32(bytes.data()+20,UINT32_MAX);put32(bytes.data()+16,~crc(bytes.data()+20,bytes.size()-20,crc(bytes.data(),16)));
  std::ofstream file(eventRoot/"events.log",std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();
  pro::events::resetForTest();assert(pro::events::workerLoad()==pro::events::Load::Loaded);
  assert(!pro::events::record(pro::events::Kind::WorkerStart,0,0,0,0));
  const auto loadedOpens=event_io::opens,loadedWrites=event_io::writes,loadedRenames=event_io::renames;
  auto maximum=application(request(UINT32_MAX-1));safeResponse(maximum);
  assert(maximum["events"][0]["s"]==UINT32_MAX&&maximum["next"]==UINT32_MAX&&maximum["newest"]==UINT32_MAX&&maximum["persisted"]==UINT32_MAX&&maximum["exhausted"]==true);
  auto maxEnd=application(request(UINT32_MAX));safeResponse(maxEnd);assert(maxEnd["events"].size()==0&&maxEnd["next"]==UINT32_MAX&&maxEnd["more"]==false);
  untouched(nvsBefore,writesBefore,loadedOpens,loadedWrites,loadedRenames);passed("UINT32_MAX sequence, persisted cursor and exhaustion use real loaded snapshot without overflow or export I/O");

  resetHistory();fs::create_directories(eventRoot);{std::ofstream corrupt(eventRoot/"events.log");corrupt<<"synthetic corrupt history";}
  assert(pro::events::workerLoad()==pro::events::Load::Corrupt);
  assert(pro::events::record(pro::events::Kind::Error,-1,0,0,1));event_io::failure="rename";
  assert(pro::events::workerFlush()==pro::events::Flush::IOError);event_io::failure.clear();
  const auto faultOpens=event_io::opens,faultWrites=event_io::writes,faultRenames=event_io::renames;
  auto fault=application(request(0));safeResponse(fault);assert(fault["corrupt"]==true&&fault["io_failed"]==true&&fault["dirty"]==true&&fault["loaded"]==true&&fault["persisted"]==0);
  untouched(nvsBefore,writesBefore,faultOpens,faultWrites,faultRenames);passed("corrupt and failed-persistence facts are reported honestly without retrying storage from BLE");
  std::cout<<"maximum_plaintext_bytes="<<maximumPlaintext<<" maximum_envelope_bytes="<<maximumEnvelope<<'\n';
#else
  pro::wifi_setup::WireRequest parsed;
  const std::string raw="{\"v\":2,\"id\":1,\"op\":\"event_log\",\"cursor\":0}";
  assert(!pro::wifi_setup::parseWire(reinterpret_cast<const uint8_t*>(raw.data()),raw.size(),parsed));
  const auto status=application("{\"op\":\"status\"}");assert(status["ok"]==true);
  assert(!pro::events::record(pro::events::Kind::Error,-1,0,0,0)&&pro::events::workerLoad()==pro::events::Load::Disabled);
  assert(testNvs==nvsBefore&&nvsWrites==writesBefore&&usbInitializations==1);passed("event-disabled actual owner runtime preserves legacy status and refuses event wire operation");
#endif
  assert(usbPolls>usbBefore&&usbInitializations==1&&testNvs==nvsBefore&&nvsWrites==writesBefore);
  for(const auto& line:USBSerial.lines)for(const char* value:{"synthetic-cloud-password","synthetic-wifi-password","private_fixture"})assert(line.find(value)==std::string::npos);
  passed("USB enumeration unchanged, callbacks continue, synthetic NVS/settings untouched and no secret diagnostics");
  std::cout<<cases<<" actual export runtime cases passed\n";
}
