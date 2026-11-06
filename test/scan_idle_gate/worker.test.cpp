// Actual sync_engine.cpp + pro_core.cpp with synthetic RTOS/SD/DAV adapters.
#include "fakes.h"
#include "backup_worker.h"
#include <dirent.h>
#include <sys/stat.h>
#include <iostream>
#include <cerrno>
#include <memory>
#include "../../src/sync_engine.cpp"
namespace {
struct Directory {unsigned index=0;};
std::string directoryMode;
template<class P>void until(P predicate){
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(6);
  while(!predicate()){assert(std::chrono::steady_clock::now()<end);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
CloudConfig cfg(){CloudConfig c;c.is_valid=true;c.watch_folder="QA";
 c.server_url="https://synthetic.invalid/dav/";c.username="synthetic";
 c.password="test-only";c.ca_pem="synthetic-ca";c.sync_interval_s=1;return c;}
void retire(){
 const auto generation=backup_worker_request_retire();assert(generation);
 until([&]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});
 until([]{return backup_worker_reap();});
 assert(fake::tasks.empty()&&fake::allocations.empty()&&!fake::snapshotOpen);
}
void awaitIdle(){until([]{return std::string(backup_status().message)=="Up to date; no USB changes";});}
void quietCheck(unsigned count=40){
 const auto hashed=fake::fingerprints.load(),mounted=fake::mounts.load();
 const auto before=backup_status();const auto ticks=fake::clock.load();
 until([&]{return fake::clock.load()>=ticks+count;});
 const auto after=backup_status();
 assert(fake::fingerprints==hashed&&fake::mounts==mounted);
 assert(after.verified==before.verified&&after.lastBackup==before.lastBackup&&after.transferred==before.transferred);
 assert(!after.active&&!after.scanning&&after.scanComplete);
}
}
extern "C" DIR* __wrap_opendir(const char* path){
 assert(std::string(path)=="/sdcard/QA"&&fake::snapshotOpen);return reinterpret_cast<DIR*>(new Directory);
}
extern "C" dirent* __wrap_readdir(DIR* raw){
 auto* dir=reinterpret_cast<Directory*>(raw);
 const unsigned count=directoryMode=="file-limit"?129:directoryMode=="entry-limit"?2050:1;
 if(dir->index>=count){if(directoryMode=="directory-error")errno=EIO;return nullptr;}
 static thread_local dirent entry{};
 if(directoryMode=="entry-limit")std::strcpy(entry.d_name,".");
 else if(directoryMode=="file-limit")std::snprintf(entry.d_name,sizeof entry.d_name,"file-%u.pdf",dir->index);
 else std::strcpy(entry.d_name,"file.pdf");
 ++dir->index;return &entry;
}
extern "C" int __wrap_closedir(DIR* raw){
 delete reinterpret_cast<Directory*>(raw);if(directoryMode=="close-error"){errno=EIO;return -1;}return 0;
}
extern "C" int __wrap_stat(const char* path,struct stat* out){
 assert(std::string(path).find("/sdcard/QA/file")==0);std::memset(out,0,sizeof(*out));out->st_mode=S_IFREG;
 std::lock_guard<std::mutex> lock(fake::mutex);out->st_size=fake::data.size();return 0;
}
int main(int argc,char** argv){
 assert(argc==2);const std::string mode=argv[1];
#if CSP_ENABLE_SCAN_IDLE_GATE
 if(mode=="scope-fields"){
   const auto original=cfg();const auto scope=scan_scope(original);
   for(auto field:{&CloudConfig::ssid,&CloudConfig::wifi_password,&CloudConfig::server_url,
       &CloudConfig::username,&CloudConfig::password,&CloudConfig::ca_pem,&CloudConfig::ntp_server,
       &CloudConfig::watch_folder,&CloudConfig::backup_mode,&CloudConfig::timezone}){
     auto changed=original;changed.*field+="changed";assert(scan_scope(changed)!=scope);
   }
   for(auto field:{&CloudConfig::sync_interval_s,&CloudConfig::max_file_size_mb,
       &CloudConfig::idle_seconds,&CloudConfig::window_start,&CloudConfig::window_end}){
     auto changed=original;++(changed.*field);assert(scan_scope(changed)!=scope);
   }
   auto changed=original;changed.is_valid=false;assert(scan_scope(changed)!=scope);
   changed=original;changed.wifi_open=true;assert(scan_scope(changed)!=scope);
   std::cout<<"PASS actual scope fingerprint covers all17 CloudConfig fields\n";return 0;
 }
#endif
 fake::wifi=WL_CONNECTED;fake::allowSnapshot=true;fake::capacityOk=true;
 directoryMode=mode;
#if CSP_ENABLE_SCAN_IDLE_GATE
 const auto currentConfig=cfg();
 const auto currentIdentity=hash_text(currentConfig.server_url+"\n"+currentConfig.username);
 const std::string oldPath="OLD-SYNTHETIC/removed.pdf",oldKey=hash_text(oldPath);
 auto oldRecord=[&](pro::State state,bool foreign){
   pro::Record record;record.path=oldPath;record.digest=hash_text("old synthetic bytes");
   record.destination=foreign?hash_text("old synthetic destination"):currentIdentity;
   record.size=19;record.sequence=1;record.state=state;record.verifiedAt=1767225600;return record;
 };
 if(mode=="inventory-global"){
   pro::FileJournal journal("SYNTHETIC-IN-MEMORY");
   for(auto state:{pro::State::Pending,pro::State::Transferred,pro::State::Verified})
     for(bool foreign:{false,true}){
       pro::receipts.clear();pro::receipts[oldKey]=oldRecord(state,foreign);
       const auto proof=scan_metadata_proof(journal,currentIdentity);
       if(!pro::scan::digest(proof))std::cout<<"REPRODUCED whole-journal rejection despite no current local pending work: state="<<unsigned(state)<<" foreign="<<foreign<<std::endl;
       assert(pro::scan::digest(proof));
     }
   pro::receipts.clear();
   for(unsigned i=0;i<=pro::FileLimit;++i){
     auto record=oldRecord(pro::State::Pending,true);record.path="OLD-SYNTHETIC/"+std::to_string(i)+".pdf";
     pro::receipts[hash_text(record.path)]=record;
   }
   assert(scan_metadata_proof(journal,currentIdentity).empty());
   pro::receipts.clear();assert(scan_metadata_proof(journal,"invalid identity").empty());
   std::cout<<"PASS inventory proof hashes every valid semantic receipt regardless of state/destination\n";return 0;
 }
 if(mode=="foreign-verified"||mode=="foreign-restart-change")pro::receipts[oldKey]=oldRecord(pro::State::Verified,true);
 if(mode=="outside-pending")pro::receipts[oldKey]=oldRecord(pro::State::Pending,false);
 if(mode=="outside-transferred")pro::receipts[oldKey]=oldRecord(pro::State::Transferred,false);
 if(mode=="local-pending-record"||mode=="local-transferred-record"){
   auto record=oldRecord(mode=="local-pending-record"?pro::State::Pending:pro::State::Transferred,false);record.path="QA/file.pdf";
   record.digest=hash_text(fake::data);record.size=fake::data.size();record.next=uint64_t(time(nullptr))+60;
   pro::receipts[hash_text(record.path)]=record;
 }
 if(mode=="corrupt-inventory")fake::corruptInventory=true;
#endif
 if(mode=="pending")fake::failUpload=true;
 if(mode=="corrupt")fake::corruptReceipt=true;
 if(mode=="error")fake::journalIOError=true;
 if(mode=="interleaved")fake::onFingerprint=[]{++fake::epoch;};
 if(mode=="cancel")fake::onFingerprint=[]{set_maintenance(true);};
 if(mode=="stale-errno")fake::onFingerprint=[]{errno=EIO;};
 std::atomic<bool> proofRaceIssued{false};
 if(mode=="metadata-race")fake::onJournalNext=[&]{
   if(fake::fingerprints&& !proofRaceIssued.exchange(true))++fake::epoch;
 };
 assert(!backup_worker_invalidate_scan_cache()); // No registered owner yet.
 assert(backup_invalidate_cloud_status()); // First cloud setup has no scan proof.
 assert(backup_worker_start(cfg()));
 assert(!backup_worker_invalidate_scan_cache()); // Running worker owns cache.
 assert(!backup_invalidate_cloud_status());
#if !CSP_ENABLE_SCAN_IDLE_GATE
 assert(mode=="default-off");until([]{return fake::fingerprints.load()>=12;});
 assert(fake::mounts>=12);retire();
 std::cout<<"PASS default-off: unchanged worker still performs interval scans\n";return 0;
#else
 if(mode=="metadata-race"){
   awaitIdle();assert(proofRaceIssued&&fake::fingerprints==2&&fake::mounts==2);
   assert(fake::puts==1&&fake::verifies==1);quietCheck();retire();
   std::cout<<"PASS write during metadata proof requires another full scan\n";return 0;
 }
 if(mode=="file-limit"||mode=="entry-limit"){
   until([]{return fake::mounts.load()>=3&&!backup_status().active;});
   assert(backup_status().error==(mode=="file-limit"?-2106:-2103));retire();
   std::cout<<"PASS actual bounded scan never establishes idle proof: "<<mode<<'\n';return 0;
 }
 if(mode=="pending"||mode=="local-pending-record"||mode=="local-transferred-record"||mode=="corrupt"||mode=="corrupt-inventory"||mode=="error"||mode=="interleaved"||mode=="cancel"||
    mode=="directory-error"||mode=="close-error"){
   until([]{return fake::fingerprints.load()>=1;});
   if(mode=="cancel"){
     for(unsigned retry=2;retry<=3;++retry){
       until([]{return !fake::snapshotOpen&&!backup_status().active;});
       set_maintenance(false);until([&]{return fake::fingerprints.load()>=retry;});
     }
   }else until([]{return fake::fingerprints.load()>=3;});
   assert(std::string(backup_status().message)!="Up to date; no USB changes");
   if(mode=="local-pending-record"||mode=="local-transferred-record"){
     BackupStatus observed;until([&]{observed=backup_status();return !observed.active;});
     assert(observed.pending==1&&fake::puts==0);
   }
   retire();
   std::cout<<"PASS actual worker requires retry: "<<mode<<'\n';return 0;
 }
 awaitIdle();assert(fake::fingerprints==1&&fake::mounts==1&&fake::puts==1&&fake::verifies==1);
 const auto verifiedAt=backup_status().lastBackup;assert(verifiedAt>=1767225600ULL);
 quietCheck();
 if(mode=="foreign-verified"||mode=="outside-pending"||mode=="outside-transferred"){
   const auto hashes=fake::fingerprints.load();retire();assert(backup_worker_start(cfg()));awaitIdle();
   assert(fake::fingerprints==hashes);quietCheck();retire();
 }else if(mode=="foreign-restart-change"){
   const auto hashes=fake::fingerprints.load();retire();
   {std::lock_guard<std::mutex> lock(fake::mutex);auto& record=pro::receipts[oldKey];
    ++record.sequence;record.state=pro::State::Transferred;}
   assert(backup_worker_start(cfg()));until([&]{return fake::fingerprints.load()>hashes;});awaitIdle();
   assert(fake::fingerprints==hashes+1&&fake::puts==1);quietCheck();retire();
 }else if(mode=="idle"||mode=="stale-errno"){
   // A new health publication remains possible while SD stays unmounted.
   const auto calls=fake::healthCalls.load();fake::clock.fetch_add(pro::health::Schedule::SuccessMs+1);
   until([&]{return fake::healthCalls.load()>calls;});quietCheck();
   const auto checks=fake::checkCalls.load();assert(request_connection_check());
   until([&]{return fake::checkCalls.load()>checks;});quietCheck();retire();
 }else if(mode=="same-size"){
   {std::lock_guard<std::mutex> lock(fake::mutex);++fake::epoch;fake::data="SYNTHETIC B";}
   until([]{return fake::fingerprints.load()>=2;});awaitIdle();
   assert(fake::puts==2&&fake::verifies==2);quietCheck();retire();
 }else if(mode=="restart"){
   pro::health::Snapshot first;
   {std::lock_guard<std::mutex> lock(fake::mutex);first=fake::healthSnapshots.back();}
   retire();FakeTask stranger;fake::current=&stranger;
   assert(!backup_worker_invalidate_scan_cache());fake::current=&fake::owner;
   const auto reads=fake::fingerprints.load();assert(backup_worker_start(cfg()));awaitIdle();
   assert(fake::fingerprints==reads);quietCheck();
   const auto publications=fake::healthCalls.load();fake::clock.fetch_add(pro::health::Schedule::SuccessMs+1);
   until([&]{return fake::healthCalls.load()>publications;});
   {std::lock_guard<std::mutex> lock(fake::mutex);const auto& resumed=fake::healthSnapshots.back();
    assert(resumed.verified==first.verified&&resumed.scan_observed_at==first.scan_observed_at&&
           resumed.last_file_verified_at==first.last_file_verified_at);}
   retire();
   assert(backup_worker_invalidate_scan_cache());assert(backup_worker_start(cfg()));
   until([&]{return fake::fingerprints.load()>reads;});awaitIdle();retire();
 }else if(mode=="cloud-invalidate"){
   retire();const auto reads=fake::fingerprints.load();
   FakeTask stranger;fake::current=&stranger;
   assert(!backup_invalidate_cloud_status());fake::current=&fake::owner;
   assert(backup_invalidate_cloud_status());assert(backup_status().verified==0);
   assert(backup_worker_start(cfg()));
   until([&]{return fake::fingerprints.load()>reads;});awaitIdle();retire();
 }else if(mode=="scope"){
   retire();auto changed=cfg();changed.password="changed-synthetic-credential";
   const auto reads=fake::fingerprints.load();assert(backup_worker_start(changed));
   until([&]{return fake::fingerprints.load()>reads;});awaitIdle();retire();
 }else if(mode=="metadata"){
   retire();{std::lock_guard<std::mutex> lock(fake::mutex);++pro::receipts.begin()->second.sequence;}
   const auto reads=fake::fingerprints.load();assert(backup_worker_start(cfg()));
   until([&]{return fake::fingerprints.load()>reads;});awaitIdle();retire();
 }else if(mode=="status-clear"){
   retire();
   // Another configuration operation cleared published status; cached values
   // must not be copied back. The test accesses the actual translation unit.
   portENTER_CRITICAL(&statusMux);status=BackupStatus{};portEXIT_CRITICAL(&statusMux);
   const auto reads=fake::fingerprints.load();assert(backup_worker_start(cfg()));
   until([&]{return fake::fingerprints.load()>reads;});awaitIdle();retire();
 }else if(mode=="audit"){
   const auto reads=fake::fingerprints.load();fake::clock.fetch_add(24u*60u*60u*1000u);
   until([&]{return fake::fingerprints.load()>reads;});awaitIdle();quietCheck();retire();
 }else {assert(false&&"unknown scenario");}
 assert(backup_status().lastBackup>=verifiedAt);
 std::cout<<"PASS actual worker/core: "<<mode<<" fingerprints="<<fake::fingerprints<<" mounts="<<fake::mounts<<" health="<<fake::healthCalls
          <<" cacheRAM="<<sizeof(pro::scan::IdleGate)<<" retainedHealthRAM="<<sizeof(pro::health::WorkerReporter)<<'\n';
#endif
}
