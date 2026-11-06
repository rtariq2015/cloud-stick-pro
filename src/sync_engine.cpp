#include "pro_device.h"
#include "pro_journal.h"
#include "webdav_client.h"
#include "usb_storage.h"
#include "pro_snapshot.h"
#include "backup_worker.h"
#include "pro_event_log.h"
#include "file_history.h"
#include "remote_health_worker.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#if CSP_ENABLE_BACKUP_SETUP
#include <esp_littlefs.h>
#endif
#include <dirent.h>
#include <sys/stat.h>
#include <atomic>
#include <algorithm>
#include <ctime>
#if defined(CSP_ENABLE_FILE_INCREMENTAL_CACHE) && CSP_ENABLE_FILE_INCREMENTAL_CACHE
#include "file_incremental_cache.h"
#endif
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE && !CSP_ENABLE_SCAN_IDLE_GATE
#error "Incremental file proofs require the bounded scan/audit lifecycle"
#endif
#if CSP_ENABLE_SCAN_IDLE_GATE
#include "scan_idle_gate.h"
#include <cerrno>
#endif
static portMUX_TYPE statusMux=portMUX_INITIALIZER_UNLOCKED;
static BackupStatus status;
static std::atomic<bool> maintenance{false};
static std::atomic<unsigned> checkRequested{0};
static bool checksAllowed=true,taskStarted=false;
static ConnectionCheck checkStatus;
static CloudConfig config;
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
static bool incrementalAuditEstablished=false;
static uint32_t incrementalAuditedAt=0;
#endif
#if CSP_ENABLE_SCAN_IDLE_GATE
static pro::scan::IdleGate idleScanGate;
static pro::scan::Summary scan_summary(const BackupStatus& value){
  return {value.verified,value.pending,value.failed,value.skipped,value.transferred,
    value.lastBackup,value.hasScan,value.scanComplete,value.error};
}
static bool scan_media_ready(){
  return storage_media_detected()&&storage_owner()!=pro::Owner::Fault;
}
static std::string scan_scope(const CloudConfig& value){
  // Every CloudConfig field participates. Hash fields separately: no ambiguous
  // delimiters and no retained credentials/CA/config copy in the RAM cache.
  std::string material;material.reserve(768);
  for(const auto* field:{&value.ssid,&value.wifi_password,&value.server_url,&value.username,
      &value.password,&value.ca_pem,&value.ntp_server,&value.watch_folder,
      &value.backup_mode,&value.timezone}){
    const auto part=hash_text(std::string(field->c_str(),field->length()));
    if(!pro::scan::digest(part))return {};material+=part;
  }
  char numbers[112];snprintf(numbers,sizeof numbers,"/%u/%u/%u/%u/%u/%u/%u/%u",
    unsigned(value.is_valid),unsigned(value.wifi_open),value.sync_interval_s,
    value.max_file_size_mb,value.idle_seconds,value.window_start,value.window_end,1u);
  material+=numbers;const auto scope=hash_text(material);
  return pro::scan::digest(scope)?scope:std::string{};
}
static std::string scan_metadata_proof(pro::FileJournal& journal,const std::string& identity){
  // This proves unchanged bounded inventory, not that historical/orphan rows
  // belong to the current local walk. Its completion gate separately requires
  // every current local file verified. Hash all valid rows, including their
  // state/destination; corruption, unknown rows and changes still fail closed.
  if(!pro::scan::digest(identity))return {};
  std::string proof=hash_text(std::string("scan-idle-receipts-v2\n")+identity),cursor,next;
  if(!pro::scan::digest(proof))return {};
  pro::Record record;
  for(unsigned i=0;i<=pro::FileLimit;++i){
    if(maintenance.load())return {};
    const auto loaded=journal.next(cursor,next,record);
    if(loaded==pro::Load::Missing)return proof;
    if(loaded!=pro::Load::Found||i==pro::FileLimit||next.empty()||next<=cursor)return {};
    uint8_t encoded[pro::RecordSize];if(!pro::encodeRecord(record,encoded))return {};
    const auto part=hash_text(std::string(reinterpret_cast<const char*>(encoded),sizeof encoded));
    if(!pro::scan::digest(part))return {};proof=hash_text(proof+part);
    if(!pro::scan::digest(proof))return {};cursor=next;vTaskDelay(1);
  }
  return {};
}
#endif
#if CSP_ENABLE_EVENT_LOG
static void event_record(pro::events::Kind kind,int32_t code=0,uint64_t bytes=0){
  pro::events::record(kind,code,bytes,uint64_t(time(nullptr)),millis());
}
struct EventWorkerScope {
  bool metadata;
  explicit EventWorkerScope(bool ready):metadata(ready){
    if(metadata){const auto loaded=pro::events::workerLoad();
      if(loaded!=pro::events::Load::AlreadyLoaded)event_record(pro::events::Kind::Boot,int32_t(loaded));}
    event_record(pro::events::Kind::WorkerStart);
  }
  ~EventWorkerScope(){
    // Declared before journal/remote/engine: their scopes (including any TLS
    // descriptors) unwind first. Metadata remains owned until the later guard.
    event_record(pro::events::Kind::Retirement);
    if(metadata)pro::events::workerFlush(millis());
  }
};
#endif
#if CSP_ENABLE_BACKUP_SETUP
// One pinned owner serializes create/reap. The worker never resumes after its
// terminal self-suspend; only that owner may delete its live static TCB.
static portMUX_TYPE workerMux=portMUX_INITIALIZER_UNLOCKED;
static BackupWorkerSnapshot workerState;
static TaskHandle_t workerOwner=nullptr,workerHandle=nullptr;
static BaseType_t workerCore=-1;
static StackType_t* workerStack=nullptr;
static StaticTask_t* workerTcb=nullptr;
static std::atomic<bool> workerHandoff{false},workerRetire{false};
static constexpr size_t WorkerStackBytes=16384;
static bool worker_retiring(){return workerRetire.load(std::memory_order_acquire);}
static void worker_running(){
  portENTER_CRITICAL(&workerMux);
  if(workerState.phase==BackupWorkerPhase::Starting&&!worker_retiring())workerState.phase=BackupWorkerPhase::Running;
  portEXIT_CRITICAL(&workerMux);
}
static void worker_metadata_fault(){
#if CSP_ENABLE_SCAN_IDLE_GATE
  idleScanGate.invalidate();
#endif
  portENTER_CRITICAL(&workerMux);workerState.error=-2115;portEXIT_CRITICAL(&workerMux);
  portENTER_CRITICAL(&statusMux);maintenance.store(true);checksAllowed=false;portEXIT_CRITICAL(&statusMux);
  backup_message("Internal queue could not close; setup blocked",-2115);
}
struct WorkerMetadataScope {
  bool acquired;
  explicit WorkerMetadataScope(bool mounted):acquired(mounted){}
  ~WorkerMetadataScope(){
    // Declared before journal/remote/engine: all their scopes and descriptors
    // must unwind before the worker releases its own metadata mount/cache.
    if(acquired)LittleFS.end();
    // Arduino end() returns void and only logs unregister errors. Verify the
    // pinned SDK's mounted state; never mistake attempted cleanup for release.
    if(esp_littlefs_mounted("spiffs"))worker_metadata_fault();
  }
};
#if CSP_ENABLE_FILE_HISTORY
struct FileHistoryRetirementScope {
  bool metadata;
  explicit FileHistoryRetirementScope(bool ready):metadata(ready){}
  ~FileHistoryRetirementScope(){
    if(!metadata||!worker_retiring()||!maintenance_active())return;
    // Declared before engine/remote/journal. All TLS/SD scopes have unwound;
    // this worker still exclusively owns metadata until the outer guard ends.
    pro::FileJournal journal("/littlefs/queue");
    const std::string identity=hash_text(std::string(config.server_url.c_str())+"\n"+config.username.c_str());
    pro::history::capture(journal,identity,config.watch_folder.c_str(),uint64_t(time(nullptr)));
  }
};
#endif
#endif
static pro::TransferMeter transferMeter;
const String& backup_folder(){return config.watch_folder;}
BackupPolicy backup_policy(){return {config.backup_mode!="eject",config.backup_mode=="overnight",config.idle_seconds,config.window_start,config.window_end};}
bool backup_window_open(){if(config.backup_mode!="overnight")return true;time_t now=time(nullptr);tm local{};localtime_r(&now,&local);return pro::inBackupWindow(local.tm_hour,config.window_start,config.window_end);}
BackupStatus backup_status(){portENTER_CRITICAL(&statusMux);BackupStatus copy=status;portEXIT_CRITICAL(&statusMux);return copy;}
void backup_message(const char* msg,int error){portENTER_CRITICAL(&statusMux);snprintf(status.message,sizeof status.message,"%s",msg);status.error=error;portEXIT_CRITICAL(&statusMux);}
void backup_transfer_begin(pro::TransferPhase phase,uint64_t total){transferMeter.start(total,millis());portENTER_CRITICAL(&statusMux);status.phase=phase;status.progress=0;status.transferDone=0;status.transferTotal=total;status.transferSpeed=0;portEXIT_CRITICAL(&statusMux);}
void backup_progress(uint64_t done,uint64_t total){transferMeter.update(done,millis());portENTER_CRITICAL(&statusMux);status.progress=transferMeter.percent();status.transferDone=transferMeter.done;status.transferTotal=total;status.transferSpeed=transferMeter.bytesPerSecond;portEXIT_CRITICAL(&statusMux);}
void set_maintenance(bool enabled){portENTER_CRITICAL(&statusMux);maintenance.store(enabled);checksAllowed=!enabled;portEXIT_CRITICAL(&statusMux);}
bool maintenance_active(){return maintenance.load();}
void allow_connection_checks(bool enabled){portENTER_CRITICAL(&statusMux);checksAllowed=enabled;portEXIT_CRITICAL(&statusMux);}
ConnectionCheck connection_check(){portENTER_CRITICAL(&statusMux);auto copy=checkStatus;portEXIT_CRITICAL(&statusMux);return copy;}
bool request_connection_check(bool uploadTest){
  portENTER_CRITICAL(&statusMux);
  bool ok=taskStarted&&checkStatus.state!=pro::CheckState::Checking&&checkRequested.load()==0;
  if(ok)checkRequested.store(uploadTest?2:1);
  if(!taskStarted){checkStatus.state=pro::CheckState::Failed;checkStatus.code=-2301;}
  portEXIT_CRITICAL(&statusMux);return ok;
}
static void checkConnection(WebDAVClient& remote){
  if(!checkRequested.load())return;
  bool wifi=WiFi.status()==WL_CONNECTED,clock=time(nullptr)>=1767225600;
  portENTER_CRITICAL(&statusMux);
  if(!checksAllowed){portEXIT_CRITICAL(&statusMux);return;}
  checkStatus.state=!wifi?pro::CheckState::WaitingWifi:!clock?pro::CheckState::WaitingClock:pro::CheckState::Checking;
  checkStatus.code=!wifi?-2302:!clock?-2303:0;
  if(!wifi||!clock){portEXIT_CRITICAL(&statusMux);return;}
  status.active=true;status.phase=pro::TransferPhase::None;unsigned operation=checkRequested.exchange(0);portEXIT_CRITICAL(&statusMux);
  backup_message(operation==2?"Testing cloud upload/readback":"Checking cloud connection");
  auto result=remote.checkConnection();char testFile[72]{};
  if(result.ok&&operation==2)result=remote.testConnection(testFile,sizeof testFile);
  portENTER_CRITICAL(&statusMux);
  checkStatus.state=result.ok?(operation==2?pro::CheckState::Verified:pro::CheckState::Ready):pro::CheckState::Failed;
  checkStatus.code=result.code;checkStatus.checkedAt=time(nullptr);
  snprintf(checkStatus.testFile,sizeof checkStatus.testFile,"%s",testFile);
  status.cloud=result.ok;status.active=false;
  portEXIT_CRITICAL(&statusMux);
  backup_message(result.ok?(operation==2?"Cloud test verified":"Cloud ready; automatic backup"):pro::errorHelp(result.code),result.ok?0:result.code);
#if CSP_ENABLE_EVENT_LOG
  // RAM only. HTTP/TLS method-local objects have already closed. This code is
  // a connection-check result, never a receipt or a new backup-success claim.
  event_record(pro::events::Kind::CloudCheck,result.ok?0:result.code);
#endif
}
struct DeviceLocal:pro::Local {
  bool fingerprint(const std::string& p,std::string& h,uint64_t& n)override{return hash_file(p,h,n);}
  std::string key(const std::string& p)override{return hash_text(p);}
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
  pro::incremental::Store cache{"/littlefs/incremental-v1.bin"};
  pro::incremental::Digest scope{};bool scopeValid=false,forceHash=true;
  void configure(const std::string& value){scopeValid=pro::incremental::digest(value,scope);}
  // Keep the512B canonical receipt scratch out of the later FIL mapping frame.
  __attribute__((noinline)) bool receipt(const pro::Record& record,pro::incremental::Entry& entry){
    if(!scopeValid||record.state!=pro::State::Verified)return false;
    uint8_t bytes[pro::RecordSize];if(!pro::encodeRecord(record,bytes))return false;
    entry.scope=scope;entry.size=record.size;
    return pro::incremental::digest(hash_text(record.path),entry.key)&&
      pro::incremental::digest(hash_text(std::string(reinterpret_cast<const char*>(bytes),sizeof bytes)),entry.receipt);
  }
  bool reuseVerified(const pro::Record& record)override{
    if(forceHash||maintenance.load())return false;
    pro::incremental::Entry expected,cached;if(!receipt(record,expected)||!cache.load(expected.key,cached))return false;
    StorageFileLayout layout;if(!storage_file_layout(record.path,&cached.stamp,layout,[](){return maintenance.load();}))return false;
    expected.stamp=layout.stamp;
    return layout.clean&&layout.size==record.size&&layout.digest==cached.layout&&
      pro::incremental::eligible(cached,expected,millis())&&storage_ready()&&!maintenance.load();
  }
  void rememberVerified(const pro::Record& record)override{
    if(maintenance.load())return;pro::incremental::Entry entry;if(!receipt(record,entry))return;
    StorageFileLayout layout;if(!storage_file_layout(record.path,nullptr,layout,[](){return maintenance.load();})||layout.size!=record.size)return;
    entry.stamp=layout.stamp;entry.layout=layout.digest;entry.auditedAt=millis();
    // Optional sidecar only: do not rewrite receipts, or write on a cache hit.
    if(storage_ready()&&!maintenance.load())(void)cache.save(entry);
  }
#endif
};
struct Scan {
  pro::Backup& engine;uint32_t seen=0,entries=0;uint32_t maxMB;
#if CSP_ENABLE_REMOTE_HEALTH
  pro::health::WorkerReporter* health=nullptr;
#endif
#if CSP_ENABLE_SCAN_IDLE_GATE
  bool complete=true;
#endif
  void directory(const std::string& relative,unsigned depth=0) {
#if CSP_ENABLE_SCAN_IDLE_GATE
    // Any partial/cancelled walk is ineligible even if a later message clears
    // status.error. Never infer a clean baseline from the final UI string.
    if(depth>=8||maintenance.load()||!storage_ready()||!backup_window_open())complete=false;
#endif
    if(depth>=8 || maintenance.load()||!storage_ready()||!backup_window_open()){if(depth>=8){backup_message("Folder depth limit (8); files retained",-2101);}return;}
    DIR* dir=opendir(("/sdcard/"+relative).c_str());if(!dir){
#if CSP_ENABLE_SCAN_IDLE_GATE
      complete=false;
#endif
      backup_message("Create the backup folder on USB, then retry",-2102);return;}
    dirent* e;
#if CSP_ENABLE_SCAN_IDLE_GATE
    int directoryReadError=0;
    auto nextEntry=[&](){
      errno=0;auto* entry=readdir(dir);if(!entry)directoryReadError=errno;return entry;
    };
    while(!maintenance.load()&&storage_ready()&&backup_window_open()&&(e=nextEntry())) {
#else
    while(!maintenance.load()&&storage_ready()&&backup_window_open()&&(e=readdir(dir))) {
#endif
      if(++entries>2048){backup_message("Directory entry limit reached; files retained",-2103);break;}
      std::string name=e->d_name;if(name=="."||name=="..")continue;
      if(relative.empty()&&(name==".cloud-stick"||name=="System Volume Information"||name=="$RECYCLE.BIN"))continue;
      std::string path=relative.empty()?name:relative+"/"+name;
      if(!pro::validPath(path)){portENTER_CRITICAL(&statusMux);++status.skipped;portEXIT_CRITICAL(&statusMux);backup_message("Unsupported path; file retained",-2104);continue;}
      struct stat st;
      if(stat(("/sdcard/"+path).c_str(),&st)!=0){
#if CSP_ENABLE_SCAN_IDLE_GATE
        complete=false;
#endif
        backup_message("SD stat failed",-2105);continue;}
      if(S_ISDIR(st.st_mode)){directory(path,depth+1);continue;}
      if(!S_ISREG(st.st_mode)){
#if CSP_ENABLE_SCAN_IDLE_GATE
        complete=false;
#endif
        continue;}
      if(seen>=pro::FileLimit){
#if CSP_ENABLE_SCAN_IDLE_GATE
        complete=false;
#endif
        backup_message("File limit (128); files retained",-2106);break;}
      ++seen;
      if(st.st_size<0||uint64_t(st.st_size)>uint64_t(maxMB)*1024*1024){portENTER_CRITICAL(&statusMux);++status.skipped;portEXIT_CRITICAL(&statusMux);backup_message("File exceeds configured limit; retained",-2107);continue;}
      // Queue even offline: failure/retry state is persisted before attempting transport.
      portENTER_CRITICAL(&statusMux);snprintf(status.currentFile,sizeof status.currentFile,"%s",path.c_str());portEXIT_CRITICAL(&statusMux);
      auto result=engine.process(path,uint64_t(time(nullptr)));
#if CSP_ENABLE_SCAN_IDLE_GATE
      if(result.outcome!=pro::Outcome::Verified||result.error||result.repaired)complete=false;
#endif
      if(!storage_ready()||maintenance.load()){backup_message(maintenance.load()?"Backup paused for setup":"USB copying; retry when idle");break;}
      uint32_t stackFree=uxTaskGetStackHighWaterMark(nullptr);
      uint32_t minimumHeap=ESP.getMinFreeHeap();
      uint32_t largestBlock=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      portENTER_CRITICAL(&statusMux);
      if(result.outcome==pro::Outcome::Verified){++status.verified;status.lastBackup=std::max(status.lastBackup,result.verifiedAt);if(result.transferred)status.cloud=true;}
      else {++status.pending;if(result.outcome==pro::Outcome::Failed||result.outcome==pro::Outcome::MetadataError||result.outcome==pro::Outcome::LocalError){++status.failed;status.cloud=false;}}
      if(result.outcome==pro::Outcome::Transferred)++status.transferred;
      if(result.error)status.error=result.error;
      status.stackFree=stackFree;status.minimumHeap=minimumHeap;status.largestBlock=largestBlock;
      portEXIT_CRITICAL(&statusMux);
#if CSP_ENABLE_REMOTE_HEALTH
      if(health)health->scanProgress(backup_status());
#endif
      if(result.outcome==pro::Outcome::MetadataError)backup_message("Internal queue unavailable/full; file retained",-2108);
      else if(result.outcome==pro::Outcome::LocalError)backup_message("SD read failed; file retained",-2109);
      else if(result.outcome==pro::Outcome::Failed)backup_message("Backup failed; automatic retry scheduled",result.error);
      if(result.repaired)backup_message("Corrupt receipt requeued conservatively",-2110);
      vTaskDelay(1);
    }
#if CSP_ENABLE_SCAN_IDLE_GATE
    if(directoryReadError){complete=false;backup_message("SD directory read failed; retrying",-2105);}
    if(maintenance.load()||!storage_ready()||!backup_window_open())complete=false;
    // Bounds/skips may not leave an error after later successful file work.
    if(entries>2048)complete=false;
    if(closedir(dir)!=0){complete=false;backup_message("SD directory close failed; retrying",-2105);}
#else
    closedir(dir);
#endif
  }
};
static void run_worker() {
#if CSP_ENABLE_BACKUP_SETUP
  // A pause immediately following admission can prevent even journal startup.
  // Retirement can cancel a delayed task handoff without opening metadata.
  while(maintenance.load()&&!worker_retiring())vTaskDelay(pdMS_TO_TICKS(25));
  if(worker_retiring())return;
  // A pre-existing mount is not ours to release. The combined runtime grants
  // metadata ownership solely to this worker; an unexpected owner stays blocked.
  if(esp_littlefs_mounted("spiffs")){worker_metadata_fault();return;}
#endif
  // Never autoformat internal metadata storage or the SD card.
  bool journalReady=LittleFS.begin(false,"/littlefs",4,"spiffs");int journalError=journalReady?-2112:-2111;
#if CSP_ENABLE_BACKUP_SETUP
  WorkerMetadataScope metadata(journalReady);
#if CSP_ENABLE_FILE_HISTORY
  FileHistoryRetirementScope history(journalReady);
#endif
  if(!journalReady&&esp_littlefs_mounted("spiffs"))return;
  if(worker_retiring())return;
#endif
  if(journalReady&&!LittleFS.exists("/queue"))journalReady=LittleFS.mkdir("/queue");
#if CSP_ENABLE_BACKUP_SETUP
  if(worker_retiring())return;
#endif
#if CSP_ENABLE_EVENT_LOG
  EventWorkerScope eventWorker(journalReady);
#endif
  DeviceLocal local;pro::FileJournal journal("/littlefs/queue");WebDAVClient remote(config);
  std::string identity=hash_text(std::string(config.server_url.c_str())+"\n"+config.username.c_str());
#if CSP_ENABLE_SCAN_IDLE_GATE
  const auto idleScope=scan_scope(config);
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
  local.configure(idleScope);
#endif
  idleScanGate.resume(storage_generation(),idleScope,
    journalReady?scan_metadata_proof(journal,identity):std::string{},
    scan_summary(backup_status()),journalReady&&scan_media_ready());
#endif
  pro::Backup engine(local,journal,remote,identity,[]()->uint64_t{return uint64_t(time(nullptr));});
#if CSP_ENABLE_REMOTE_HEALTH
#if CSP_ENABLE_SCAN_IDLE_GATE
  // Reporter owns observations/schedule only, never sockets or worker refs.
  // Reuse original observation times across a proven same-boot restart; do not
  // fabricate scanEnded/verified timestamps merely because SD was unchanged.
  static pro::health::WorkerReporter retainedHealth;
  if(!idleScanGate.valid())retainedHealth=pro::health::WorkerReporter{};
  auto& health=retainedHealth;
#else
  pro::health::WorkerReporter health;
#endif
  // Owner BLE/public routing waits for this entire worker to retire and be
  // reaped. No admission may bypass maintenance/retirement or an SD read view.
  auto healthTick=[&](){health.tick(remote,!maintenance.load()&&!worker_retiring()&&!checkRequested.load());};
#endif
  // Recover the latest receipt time without treating historic files as a fresh
  // SD scan. Bounded enumeration uses only one record at a time.
  if(journalReady){
    std::string cursor,next;pro::Record record;uint64_t latest=0;
    for(unsigned i=0;i<pro::FileLimit
#if CSP_ENABLE_BACKUP_SETUP
        &&!worker_retiring()
#endif
        ;++i){
      auto result=journal.next(cursor,next,record);if(result==pro::Load::Missing||result==pro::Load::IOError)break;cursor=next;
      if(result==pro::Load::Found&&record.destination==identity&&record.state==pro::State::Verified&&record.path.compare(0,config.watch_folder.length()+1,std::string(config.watch_folder.c_str())+"/")==0)latest=std::max(latest,record.verifiedAt);
      vTaskDelay(1);
    }
    portENTER_CRITICAL(&statusMux);status.lastBackup=latest;portEXIT_CRITICAL(&statusMux);
  }

#if CSP_ENABLE_BACKUP_SETUP
  if(worker_retiring())return;
  worker_running();
#endif
  for(;;) {
#if CSP_ENABLE_BACKUP_SETUP
    if(worker_retiring())break;
#endif
#if CSP_ENABLE_EVENT_LOG
    // An idle/scan boundary: no SD reader is held and the previous network
    // call's stack-owned HTTP/TLS objects have unwound. Never flush progress.
    if(journalReady)pro::events::workerFlush(millis());
#endif
    checkConnection(remote);
    if(!journalReady){backup_message("Internal backup journal unavailable",journalError);
#if CSP_ENABLE_REMOTE_HEALTH
      healthTick();
#endif
#if CSP_ENABLE_EVENT_LOG
      event_record(pro::events::Kind::Error,journalError);
#endif
      vTaskDelay(pdMS_TO_TICKS(1000));continue;}
    if(maintenance.load()){vTaskDelay(pdMS_TO_TICKS(250));continue;}
    if(WiFi.status()!=WL_CONNECTED){backup_message("Wi-Fi disconnected; reconnecting",-2302);
#if CSP_ENABLE_EVENT_LOG
      event_record(pro::events::Kind::Error,-2302);
#endif
      vTaskDelay(pdMS_TO_TICKS(1000));continue;}
    if(time(nullptr)<1767225600){backup_message("Waiting for network time",-2303);
#if CSP_ENABLE_EVENT_LOG
      event_record(pro::events::Kind::Error,-2303);
#endif
      vTaskDelay(pdMS_TO_TICKS(1000));continue;}
    if(!backup_window_open()){backup_message("Waiting for overnight window");
#if CSP_ENABLE_REMOTE_HEALTH
      healthTick();
#endif
      vTaskDelay(pdMS_TO_TICKS(1000));continue;}
    auto policy=backup_policy();
    if(storage_owner()==pro::Owner::Host&&!policy.background){backup_message("Waiting for USB safe eject");
#if CSP_ENABLE_REMOTE_HEALTH
      healthTick();
#endif
      vTaskDelay(pdMS_TO_TICKS(1000));continue;}
    if(storage_quiet_remaining(policy.idleSeconds)){backup_message("Waiting for copying to stop");
#if CSP_ENABLE_REMOTE_HEALTH
      healthTick();
#endif
      vTaskDelay(pdMS_TO_TICKS(500));continue;}
#if CSP_ENABLE_SCAN_IDLE_GATE
    if(
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
       incrementalAuditEstablished&&uint32_t(millis()-incrementalAuditedAt)<pro::incremental::AuditMs&&
#endif
       idleScanGate.skip(storage_generation(),idleScope,millis(),
                         scan_summary(backup_status()),scan_media_ready())){
      // Preserve receipt counts and lastBackup. No mount, fingerprint, synthetic
      // verifiedAt, scanStarted or scanEnded event occurs on this path.
      backup_message("Up to date; no USB changes");
#if CSP_ENABLE_REMOTE_HEALTH
      healthTick();
#endif
      vTaskDelay(pdMS_TO_TICKS(1000));continue;
    }
    // Once a scan is needed, failures/cancellation cannot retain older proof.
    idleScanGate.invalidate();
#endif
    portENTER_CRITICAL(&statusMux);
    if(maintenance.load()){portEXIT_CRITICAL(&statusMux);continue;}
    status.active=true;status.scanning=true;status.scanComplete=false;status.phase=pro::TransferPhase::None;status.transferDone=0;status.transferTotal=0;status.transferSpeed=0;status.pending=0;status.failed=0;status.verified=0;status.transferred=0;status.skipped=0;status.error=0;status.progress=-1;portEXIT_CRITICAL(&statusMux);
    if(!storage_begin_read(policy.idleSeconds,policy.background)){
      portENTER_CRITICAL(&statusMux);status.active=false;status.scanning=false;portEXIT_CRITICAL(&statusMux);
      backup_message(storage_quiet_remaining(policy.idleSeconds)?"USB copying; retry when idle":"Cannot read SD view; retrying");
#if CSP_ENABLE_REMOTE_HEALTH
      healthTick();
#endif
      vTaskDelay(pdMS_TO_TICKS(1000));continue;
    }
    backup_message("Checking your QA files; originals stay on USB");
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
    // A daily full pass is independent of per-file hits and survives a clean
    // same-boot BLE worker retirement. New files cannot postpone the audit.
    local.forceHash=!incrementalAuditEstablished||uint32_t(millis()-incrementalAuditedAt)>=pro::incremental::AuditMs;
#endif
#if CSP_ENABLE_SCAN_IDLE_GATE
    const uint64_t scanBeginGeneration=storage_generation();
#endif
    uint64_t total=0,free=0;bool capacity=storage_capacity(total,free);
#if CSP_ENABLE_REMOTE_HEALTH
    health.scanStarted();health.capacityMeasured(capacity,total,free);
#endif
    portENTER_CRITICAL(&statusMux);status.total=total;status.free=free;status.capacityKnown=capacity;portEXIT_CRITICAL(&statusMux);
    Scan scan{engine,0,0,config.max_file_size_mb
#if CSP_ENABLE_REMOTE_HEALTH
      ,&health
#endif
    };scan.directory(config.watch_folder.c_str());
    bool consistent=storage_ready();storage_end_read();
#if CSP_ENABLE_SCAN_IDLE_GATE
    const uint64_t scanEndGeneration=storage_generation();
#endif
    // localtime_r acquires libc locks, including the environment's recursive
    // mutex. ESP-IDF aborts on that call with interrupts disabled. Compute the
    // schedule outside the status spinlock, including for overnight mode.
    bool windowOpen=backup_window_open();
    portENTER_CRITICAL(&statusMux);status.currentFile[0]=0;status.scanning=false;status.active=false;status.hasScan=true;status.scanComplete=consistent&&!maintenance.load()&&windowOpen&&!status.error&&!status.skipped
#if CSP_ENABLE_SCAN_IDLE_GATE
      &&scan.complete
#endif
      ;
    if(!consistent){status.error=0;status.capacityKnown=false;}
    if(!status.error)snprintf(status.message,sizeof status.message,"%s",!consistent?"USB copying; retry when idle":maintenance.load()?"Scan paused; counts partial":!scan.seen?"Backup folder is empty":status.pending?"Waiting for next backup retry":status.skipped?"Scan has skipped files":"Scan complete; files verified");portEXIT_CRITICAL(&statusMux);
#if CSP_ENABLE_SCAN_IDLE_GATE
    const auto baseline=backup_status();
    if(scan.complete&&baseline.scanComplete&&!baseline.pending&&!baseline.failed&&
       !baseline.skipped&&!baseline.error){
      const auto proof=scan_metadata_proof(journal,identity);
      // A USB write while receipt proof is being read must not mark its newer
      // generation clean. A later write still differs from the stored begin.
      const auto baselineEndGeneration=storage_generation();
      idleScanGate.completed(scanBeginGeneration,baselineEndGeneration,idleScope,proof,
        millis(),scan_summary(backup_status()),scan.seen,
        consistent&&scanBeginGeneration==scanEndGeneration&&!maintenance.load()&&
          windowOpen&&scan.complete,scan_media_ready());
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
      if(local.forceHash&&scanBeginGeneration==baselineEndGeneration&&consistent&&
         !maintenance.load()&&windowOpen&&!proof.empty()){
        incrementalAuditEstablished=true;incrementalAuditedAt=millis();
      }
#endif
    }
#endif
#if CSP_ENABLE_REMOTE_HEALTH
    health.scanEnded(backup_status(),consistent);healthTick();
#endif
#if CSP_ENABLE_EVENT_LOG
    const auto completed=backup_status();
    event_record(pro::events::Kind::Error,completed.error); // Explicit recovery lets a repeated later error be recorded.
    event_record(pro::events::Kind::VerifiedCount,int32_t(completed.verified));
    event_record(pro::events::Kind::Retry,completed.pending?(completed.error?completed.error:-2012):0);
    event_record(pro::events::Kind::ScanOutcome,completed.scanComplete?0:completed.error?completed.error:-2010);
    // Counts describe this scan only. No filenames, destination, credentials or
    // unmeasured transfer byte estimates enter diagnostics. Receipts stay truth.
    pro::events::workerFlush(millis());
#endif
    // Wake frequently for maintenance requests; retry times remain per-file and persistent.
    uint32_t elapsed=0;while(elapsed<config.sync_interval_s&&!maintenance.load()&&!checkRequested.load()){
#if CSP_ENABLE_REMOTE_HEALTH
      healthTick();
#endif
      vTaskDelay(pdMS_TO_TICKS(1000));++elapsed;}
  }
}
#if CSP_ENABLE_BACKUP_SETUP
static void task(void*) {
  // xTaskCreateStaticPinnedToCore may run us before it returns to the owner.
  // No config/read/TLS work is admitted before the owner publishes the handle.
  while(!workerHandoff.load(std::memory_order_acquire))vTaskDelay(1);
  run_worker(); // All journal/remote/engine/TLS destructors run before Drained.
  portENTER_CRITICAL(&statusMux);
  taskStarted=false;checkRequested.store(0);checksAllowed=false;
  status.active=false;status.scanning=false;status.currentFile[0]=0;
  portEXIT_CRITICAL(&statusMux);
  portENTER_CRITICAL(&workerMux);
  workerState.retired_generation=workerState.generation;
  workerState.phase=BackupWorkerPhase::Drained;
  portEXIT_CRITICAL(&workerMux);
  // Never return from a FreeRTOS task entry. Do not hold any lock here. The
  // owner must independently observe eSuspended before deleting/freeing us.
  for(;;)vTaskSuspend(nullptr);
}

static bool worker_owner(){
  return workerOwner==xTaskGetCurrentTaskHandle()&&workerCore==xPortGetCoreID()&&
    xTaskGetCoreID(workerOwner)==workerCore;
}
static void worker_failed(int error){
  set_maintenance(true);
  portENTER_CRITICAL(&statusMux);taskStarted=false;checkRequested.store(0);portEXIT_CRITICAL(&statusMux);
  portENTER_CRITICAL(&workerMux);workerState.phase=BackupWorkerPhase::Failed;workerState.error=error;portEXIT_CRITICAL(&workerMux);
  backup_message("Cannot allocate backup task",error);
}
BackupWorkerSnapshot backup_worker_snapshot(){
  portENTER_CRITICAL(&workerMux);const auto copy=workerState;portEXIT_CRITICAL(&workerMux);return copy;
}
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
bool backup_invalidate_cloud_status(){
  if(backup_worker_snapshot().phase!=BackupWorkerPhase::Stopped)return false;
  // Before the first worker there is no scan proof to retire. After binding,
  // only its pinned owner may discard that proof, with every worker reaped.
  if(workerOwner&&!backup_worker_invalidate_scan_cache())return false;
  portENTER_CRITICAL(&statusMux);status=BackupStatus{};checkStatus=ConnectionCheck{};portEXIT_CRITICAL(&statusMux);
  return true;
}
#endif
bool backup_worker_start(const CloudConfig& c){
  if(!c.is_valid)return false;
  const auto owner=xTaskGetCurrentTaskHandle();const auto core=xPortGetCoreID();
  // An unpinned owner could migrate between the suspended-state proof and
  // deletion. Refuse that ownership contract rather than free a remote task.
  if(!owner||xTaskGetCoreID(owner)!=core)return false;
  portENTER_CRITICAL(&workerMux);
  if((workerOwner&&workerOwner!=owner)||workerState.phase!=BackupWorkerPhase::Stopped||
     workerHandle||workerStack||workerTcb||workerState.generation==UINT32_MAX){
    portEXIT_CRITICAL(&workerMux);return false;
  }
  workerOwner=owner;workerCore=core;workerState.phase=BackupWorkerPhase::Starting;
  ++workerState.generation;workerState.error=0;
  workerHandoff.store(false,std::memory_order_release);workerRetire.store(false,std::memory_order_release);
  portEXIT_CRITICAL(&workerMux);
#if CSP_ENABLE_FILE_HISTORY
  // The owner has admitted a new generation. Private history must not consume
  // heap during its worker startup, source hashing or subsequent TLS requests.
  pro::history::release();
#endif
  set_maintenance(true);
  workerStack=static_cast<StackType_t*>(heap_caps_malloc(WorkerStackBytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  workerTcb=static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!workerStack||!workerTcb){
    heap_caps_free(workerStack);heap_caps_free(workerTcb);workerStack=nullptr;workerTcb=nullptr;
    worker_failed(-2113);return false;
  }
  // No existing task or worker C++ scope may observe replacement config.
  config=c;
  // Arduino String allocation failure does not throw. Never admit a partially
  // copied destination/credential set as a valid immutable worker config.
  if(config.ssid!=c.ssid||config.wifi_password!=c.wifi_password||config.server_url!=c.server_url||
     config.username!=c.username||config.password!=c.password||config.ca_pem!=c.ca_pem||
     config.ntp_server!=c.ntp_server||config.watch_folder!=c.watch_folder||
     config.backup_mode!=c.backup_mode||config.timezone!=c.timezone){
    heap_caps_free(workerStack);heap_caps_free(workerTcb);workerStack=nullptr;workerTcb=nullptr;
    worker_failed(-2113);return false;
  }
  workerHandle=xTaskCreateStaticPinnedToCore(task,"Backup",WorkerStackBytes,nullptr,1,workerStack,workerTcb,workerCore);
  if(!workerHandle){
    heap_caps_free(workerStack);heap_caps_free(workerTcb);workerStack=nullptr;workerTcb=nullptr;
    worker_failed(-2113);return false;
  }
  portENTER_CRITICAL(&statusMux);
  if(!worker_retiring()){
    status.cloud=false;checkStatus=ConnectionCheck{};
    taskStarted=true;checkRequested.store(1);checksAllowed=true;maintenance.store(false);
  }
  portEXIT_CRITICAL(&statusMux);
  workerHandoff.store(true,std::memory_order_release);
  return true;
}
uint32_t backup_worker_request_retire(){
  if(workerOwner&&!worker_owner())return 0;
  portENTER_CRITICAL(&workerMux);
  const auto generation=workerState.generation;
  workerRetire.store(true,std::memory_order_release);
  if(workerState.phase==BackupWorkerPhase::Starting||workerState.phase==BackupWorkerPhase::Running)
    workerState.phase=BackupWorkerPhase::Retiring;
  portEXIT_CRITICAL(&workerMux);
  portENTER_CRITICAL(&statusMux);
  maintenance.store(true);checksAllowed=false;taskStarted=false;checkRequested.store(0);
  portEXIT_CRITICAL(&statusMux);
  return generation;
}
bool backup_worker_reap(){
  if(workerOwner&&!worker_owner())return false;
  const auto state=backup_worker_snapshot();
  if(state.phase==BackupWorkerPhase::Stopped)return true;
  if(state.phase==BackupWorkerPhase::Failed&&!workerHandle&&!workerStack&&!workerTcb){
    config=CloudConfig{};
    portENTER_CRITICAL(&workerMux);workerState.phase=BackupWorkerPhase::Stopped;portEXIT_CRITICAL(&workerMux);return true;
  }
  if(state.phase!=BackupWorkerPhase::Drained||state.error||state.retired_generation!=state.generation||!workerHandle)return false;
  // This handle remains owned/live until this deletion. No snapshot queries it,
  // and no code resumes this task. Same-core owner + eSuspended prove it cannot
  // execute or hold a worker lock. The pinned SDK deletes such a nonrunning task
  // synchronously (as distinct from self/other-core-current task deletion).
  if(eTaskGetState(workerHandle)!=eSuspended)return false;
  vTaskDelete(workerHandle);workerHandle=nullptr;
  heap_caps_free(workerStack);heap_caps_free(workerTcb);workerStack=nullptr;workerTcb=nullptr;
  // No accessor/worker may retain config references across owner-controlled
  // retirement. Release cloud strings/CA before the separate BLE heap gate.
  config=CloudConfig{};
  portENTER_CRITICAL(&workerMux);workerState.phase=BackupWorkerPhase::Stopped;portEXIT_CRITICAL(&workerMux);
  return true;
}
bool backup_worker_invalidate_scan_cache(){
  if(!workerOwner||!worker_owner())return false;
  portENTER_CRITICAL(&workerMux);
  const bool stopped=workerState.phase==BackupWorkerPhase::Stopped&&
    !workerHandle&&!workerStack&&!workerTcb;
#if CSP_ENABLE_SCAN_IDLE_GATE
  if(stopped)idleScanGate.invalidate();
#endif
  portEXIT_CRITICAL(&workerMux);
#if CSP_ENABLE_FILE_INCREMENTAL_CACHE
  if(stopped){incrementalAuditEstablished=false;storage_incremental_invalidate();}
#endif
  return stopped;
}
void start_backup(const CloudConfig& c){(void)backup_worker_start(c);}
#else
static void task(void*){run_worker();}
void start_backup(const CloudConfig& c) {
  if(!c.is_valid)return;config=c;
  taskStarted=true;checkRequested.store(1);
  if(xTaskCreate(task,"Backup",16384,nullptr,1,nullptr)!=pdPASS){taskStarted=false;checkRequested.store(0);backup_message("Cannot allocate backup task",-2113);}
}
#endif
