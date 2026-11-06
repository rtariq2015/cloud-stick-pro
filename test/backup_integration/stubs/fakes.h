#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "remote_health_publisher.h"

using String=std::string;
struct CloudConfig {
  bool is_valid=false,wifi_open=false;
  String ssid,wifi_password,server_url,username,password,ca_pem,ntp_server="pool.ntp.org";
  uint32_t sync_interval_s=60,max_file_size_mb=64;
  String watch_folder="QA",backup_mode="idle",timezone="EST5EDT,M3.2.0,M11.1.0";
  uint32_t idle_seconds=120,window_start=1,window_end=5;
};
using portMUX_TYPE=std::recursive_mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
#define pdMS_TO_TICKS(n) (n)
using BaseType_t=int;
using StackType_t=uint8_t;
struct StaticTask_t {uint8_t data[256];};
enum eTaskState {eRunning,eReady,eBlocked,eSuspended,eDeleted,eInvalid};
struct FakeTask {
  std::thread thread;
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic<eTaskState> state{eReady};
  bool deleted=false;
  int core=1;
};
using TaskHandle_t=FakeTask*;
struct TaskExit {};
namespace fake {
inline FakeTask owner;
inline thread_local TaskHandle_t current=&owner;
inline thread_local int actualCore=1;
inline std::mutex mutex;
inline std::condition_variable cv;
inline std::vector<std::string> events;
inline std::set<void*> allocations;
inline std::set<TaskHandle_t> tasks;
inline std::atomic<unsigned> clock{0},beginCalls{0},checkCalls{0},deletes{0},queries{0};
inline std::atomic<unsigned> endCalls{0},journalLive{0},remoteLive{0},engineLive{0};
inline std::atomic<bool> metadataMounted{false},beginResult{true},endFails{false},partialMount{false};
inline std::atomic<bool> holdLaunch{false},holdPark{false},holdBegin{false},holdCheck{false};
inline std::atomic<bool> allowSnapshot{false},snapshotOpen{false},snapshotEntered{false},holdSnapshot{false};
inline std::atomic<bool> beginEntered{false},checkEntered{false},failCreate{false},hideSuspended{false};
inline std::atomic<int> failAllocation{-1};
inline std::atomic<int> wifi{0};
inline std::function<void()> duringCreate;
inline std::function<void(const std::string&)> onEvent;
inline std::string remoteIdentity;
#if CSP_ENABLE_REMOTE_HEALTH
inline std::atomic<unsigned> healthCalls{0},healthDone{0},cloudTLS{0};
inline std::atomic<bool> holdHealth{false},cardInitialized{true},capacityOk{false};
inline std::atomic<int> healthCode{200};
inline std::vector<pro::health::Snapshot> healthSnapshots;
#endif
inline void event(const std::string& text){
  if(onEvent)onEvent(text);
  std::lock_guard<std::mutex> l(mutex);events.push_back(text);
}
inline void wait(const std::atomic<bool>& flag){
  std::unique_lock<std::mutex> l(mutex);cv.wait(l,[&]{return !flag.load();});
}
inline void release(std::atomic<bool>& flag){flag.store(false);cv.notify_all();}
}
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return fake::current;}
inline BaseType_t xPortGetCoreID(){return fake::actualCore;}
inline BaseType_t xTaskGetCoreID(TaskHandle_t task){return task->core;}
inline void vTaskDelay(unsigned){fake::clock.fetch_add(1);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
inline uint32_t millis(){return fake::clock.load();}
inline unsigned uxTaskGetStackHighWaterMark(void*){return 8192;}
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void(*entry)(void*),const char*,unsigned bytes,
 void* context,unsigned,StackType_t* stack,StaticTask_t* tcb,int core){
  assert(bytes==16384&&stack&&tcb&&core==fake::owner.core);
  if(fake::failCreate.exchange(false)){
    if(fake::duringCreate)fake::duringCreate();
    return nullptr;
  }
  auto task=new FakeTask;task->core=core;
  {std::lock_guard<std::mutex> l(fake::mutex);fake::tasks.insert(task);}
  task->thread=std::thread([=]{
    fake::current=task;fake::wait(fake::holdLaunch);task->state=eRunning;
    try {entry(context);assert(false&&"FreeRTOS task entry returned");}catch(const TaskExit&){}
  });
  if(fake::duringCreate)fake::duringCreate();
  return task;
}
inline eTaskState eTaskGetState(TaskHandle_t task){
  std::lock_guard<std::mutex> l(fake::mutex);assert(fake::tasks.count(task));++fake::queries;
  return fake::hideSuspended?eRunning:task->state.load();
}
inline void vTaskSuspend(TaskHandle_t task){
  assert(!task);task=fake::current;fake::wait(fake::holdPark);
  std::unique_lock<std::mutex> l(task->mutex);task->state=eSuspended;
  task->cv.wait(l,[=]{return task->deleted;});throw TaskExit{};
}
inline void vTaskDelete(TaskHandle_t task){
  assert(task&&task!=fake::current&&task->core==fake::current->core&&task->state==eSuspended);
  {std::lock_guard<std::mutex> l(task->mutex);task->deleted=true;}task->cv.notify_all();
  task->thread.join();
  {std::lock_guard<std::mutex> l(fake::mutex);assert(fake::tasks.erase(task)==1);}
  ++fake::deletes;fake::event("task deleted");delete task;
}
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t size,unsigned){
  const auto fail=fake::failAllocation.load();
  if(fail==0){fake::failAllocation=-1;return nullptr;}
  if(fail>0)--fake::failAllocation;
  void* p=std::malloc(size);if(p){std::lock_guard<std::mutex> l(fake::mutex);fake::allocations.insert(p);}return p;
}
inline void heap_caps_free(void* p){
  if(!p)return;
  {std::lock_guard<std::mutex> l(fake::mutex);assert(fake::allocations.erase(p)==1);}
  fake::event("static buffer freed");std::free(p);
}
inline uint32_t heap_caps_get_largest_free_block(unsigned){return 131060;}
struct FakeESP {uint32_t getMinFreeHeap(){return 85000;}};
inline FakeESP ESP;
#define WL_CONNECTED 3
struct FakeWiFi {int status(){return fake::wifi.load();}};
inline FakeWiFi WiFi;
struct FakeLittleFS {
  bool begin(bool format,const char*,int,const char*){
    assert(!format);++fake::beginCalls;fake::beginEntered=true;fake::wait(fake::holdBegin);
    fake::metadataMounted=fake::beginResult||fake::partialMount;return fake::beginResult;
  }
  void end(){
    assert(fake::journalLive==0&&fake::remoteLive==0&&fake::engineLive==0);
    ++fake::endCalls;
    if(fake::endFails){fake::event("metadata end failed");return;}
    fake::metadataMounted=false;fake::event("metadata unmounted");
  }
  bool exists(const char*){return true;}
  bool mkdir(const char*){assert(false&&"tests must not create persistent paths");return false;}
};
inline FakeLittleFS LittleFS;
inline bool esp_littlefs_mounted(const char* label){assert(!std::strcmp(label,"spiffs"));return fake::metadataMounted;}
namespace pro {
enum class TransferPhase {None};
enum class CheckState {Idle,Failed,Checking,WaitingWifi,WaitingClock,Verified,Ready};
struct TransferMeter {
  uint64_t done=0;uint32_t bytesPerSecond=0;
  void start(uint64_t,uint32_t){}void update(uint64_t n,uint32_t){done=n;}
  int percent(){return 0;}
};
enum class Owner {Host,Firmware
#if CSP_ENABLE_REMOTE_HEALTH
  ,Ejected=Firmware,Local,Fault
#endif
};
#if CSP_ENABLE_REMOTE_HEALTH
constexpr int32_t SnapshotChanged=-2010;
constexpr const char* RemoteHealthSyntheticVersion="synthetic-health-worker";
#endif
constexpr unsigned FileLimit=128,PathLimit=255;
inline bool inBackupWindow(int,unsigned,unsigned){return true;}
inline bool validPath(const std::string&){return true;}
inline const char* errorHelp(int){return "synthetic error";}
struct Local {virtual ~Local()=default;virtual bool fingerprint(const std::string&,std::string&,uint64_t&)=0;virtual std::string key(const std::string&)=0;};
enum class State {Verified};
enum class Load {Missing,IOError,Found};
struct Record {std::string destination,path;State state=State::Verified;uint64_t verifiedAt=0;};
struct FileJournal {
  explicit FileJournal(const char*){++fake::journalLive;fake::event("journal created");}
  ~FileJournal(){--fake::journalLive;fake::event("journal destroyed");}
  Load next(std::string&,std::string&,Record&){return Load::Missing;}
};
enum class Outcome {Verified,Failed,MetadataError,LocalError,Transferred};
struct Result {Outcome outcome=Outcome::Verified;uint64_t verifiedAt=0;bool transferred=false,repaired=false;int error=0;};
struct Backup {
  template<class Remote,class Clock>Backup(Local&,FileJournal&,Remote&,const std::string&,Clock){++fake::engineLive;fake::event("engine created");}
  ~Backup(){--fake::engineLive;fake::event("engine destroyed");}
  Result process(const std::string&,uint64_t){return {};}
};
}
struct BackupStatus {
  uint32_t pending=0,verified=0,failed=0,transferred=0,skipped=0;
  uint64_t total=0,free=0,lastBackup=0;
  uint32_t minimumHeap=0,largestBlock=0,stackFree=0;
  bool scanning=false,active=false,cloud=false,capacityKnown=false,hasScan=false,scanComplete=false;
  int progress=-1,error=0;pro::TransferPhase phase=pro::TransferPhase::None;
  uint64_t transferDone=0,transferTotal=0;uint32_t transferSpeed=0;
  char message[96]{},currentFile[pro::PathLimit+1]{};
};
struct ConnectionCheck {pro::CheckState state=pro::CheckState::Idle;int code=0;uint64_t checkedAt=0;char testFile[72]{};};
struct BackupPolicy {bool background=true,overnight=false;uint32_t idleSeconds=120,startHour=1,endHour=5;};
BackupStatus backup_status();void backup_message(const char*,int=0);void set_maintenance(bool);
bool maintenance_active();void allow_connection_checks(bool);bool request_connection_check(bool=false);
ConnectionCheck connection_check();
const String& backup_folder();BackupPolicy backup_policy();bool backup_window_open();
inline std::string hash_text(const std::string& value){return value;}
inline bool hash_file(const std::string&,std::string&,uint64_t&){return false;}
class WebDAVClient {
 public:
  CloudConfig held;
  explicit WebDAVClient(const CloudConfig& c):held(c){++fake::remoteLive;fake::remoteIdentity=c.server_url;fake::event("remote created");}
  ~WebDAVClient(){--fake::remoteLive;fake::event("remote destroyed");}
  struct Response {bool ok=true;int code=0;};
  Response checkConnection(){
#if CSP_ENABLE_REMOTE_HEALTH
    struct Count {Count(){++fake::cloudTLS;}~Count(){--fake::cloudTLS;}} count;
#endif
    struct TLS {~TLS(){fake::event("TLS destroyed");}} tls;
    ++fake::checkCalls;fake::checkEntered=true;fake::wait(fake::holdCheck);return {};
  }
  Response testConnection(char*,size_t){return {};}
#if CSP_ENABLE_REMOTE_HEALTH
  pro::health::Publication publishHealth(const pro::health::Snapshot& snapshot,const pro::health::Admission& admission){
    assert(admission.normal_worker&&admission.sd_view_unmounted&&admission.radio_exclusive);
    assert(!fake::snapshotOpen&&fake::cloudTLS==0&&!maintenance_active());
    {std::lock_guard<std::mutex> lock(fake::mutex);fake::healthSnapshots.push_back(snapshot);}
    ++fake::healthCalls;fake::event("health attempt");fake::wait(fake::holdHealth);++fake::healthDone;
    if(maintenance_active())return {false,-2010,0,0};
    return {fake::healthCode==200,fake::healthCode.load(),fake::healthCalls.load(),0};
  }
#endif
};
inline bool storage_ready(){return fake::snapshotOpen.load();}
inline pro::Owner storage_owner(){return pro::Owner::Host;}
inline unsigned storage_quiet_remaining(unsigned){return 0;}
inline bool storage_begin_read(unsigned,bool){
  if(!fake::allowSnapshot)return false;
  assert(!fake::snapshotOpen.exchange(true));fake::snapshotEntered=true;
  fake::wait(fake::holdSnapshot);return true;
}
inline void storage_end_read(){assert(fake::snapshotOpen.exchange(false));fake::event("snapshot released");}
#if CSP_ENABLE_REMOTE_HEALTH
inline bool storage_media_detected(){return fake::cardInitialized;}
inline bool storage_read_view_active(){return fake::snapshotOpen;}
inline bool storage_capacity(uint64_t& total,uint64_t& free){
  assert(fake::snapshotOpen);total=4096;free=1024;return fake::capacityOk;
}
constexpr const char* FirmwareVersion="synthetic-health-worker";
#else
inline bool storage_capacity(uint64_t&,uint64_t&){return false;}
#endif
