// Actual DeviceLocal, Backup and worker, with synthetic storage/RTOS/DAV only.
#include "fakes.h"
#include "../../include/usb_storage.h"
#include <map>
#include <cstdio>
#include <set>
static pro::incremental::Coverage covered;
static std::map<std::string,uint32_t> locations;
static std::string sidecar;
static bool mappingAllowed=true;
static std::mutex coverageMutex;
static std::set<FILE*> sidecarHandles;
static uint64_t sidecarReadBytes=0,sidecarWrites=0;
#include <dirent.h>
#include <sys/stat.h>
#include <iostream>
#include "../../src/sync_engine.cpp"
namespace {
struct Directory {bool seen=false;};
template<class P>void until(P predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(6);while(!predicate()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
CloudConfig cfg(){CloudConfig c;c.is_valid=true;c.watch_folder="QA";c.server_url="https://synthetic.invalid/dav/";c.username="synthetic";c.password="test-only";c.ca_pem="synthetic-ca";c.sync_interval_s=1;return c;}
void retire(){assert(backup_worker_request_retire());until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});until([]{return backup_worker_reap();});assert(fake::tasks.empty()&&fake::allocations.empty()&&!fake::snapshotOpen);}
void idle(){until([]{return std::string(backup_status().message)=="Up to date; no USB changes";});}
}
extern "C" DIR* __wrap_opendir(const char* path){assert(std::string(path)=="/sdcard/QA"&&fake::snapshotOpen);return reinterpret_cast<DIR*>(new Directory);}
extern "C" dirent* __wrap_readdir(DIR* raw){auto* dir=reinterpret_cast<Directory*>(raw);if(dir->seen)return nullptr;dir->seen=true;static thread_local dirent entry{};strcpy(entry.d_name,"file.pdf");return &entry;}
extern "C" int __wrap_closedir(DIR* raw){delete reinterpret_cast<Directory*>(raw);return 0;}
extern "C" int __wrap_stat(const char*,struct stat* out){memset(out,0,sizeof(*out));out->st_mode=S_IFREG;out->st_size=fake::data.size();return 0;}
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
extern "C" size_t __real_fwrite(const void*,size_t,size_t,FILE*);
extern "C" int __real_fclose(FILE*);
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){const bool cache=!strcmp(path,"/littlefs/incremental-v1.bin");FILE* result=__real_fopen(cache?sidecar.c_str():path,mode);if(cache&&result)sidecarHandles.insert(result);return result;}
extern "C" size_t __wrap_fread(void* out,size_t size,size_t count,FILE* file){const auto n=__real_fread(out,size,count,file);if(sidecarHandles.count(file))sidecarReadBytes+=n*size;return n;}
extern "C" size_t __wrap_fwrite(const void* in,size_t size,size_t count,FILE* file){const auto n=__real_fwrite(in,size,count,file);if(sidecarHandles.count(file)&&n)++sidecarWrites;return n;}
extern "C" int __wrap_fclose(FILE* file){sidecarHandles.erase(file);return __real_fclose(file);}
void storage_incremental_invalidate(){std::lock_guard<std::mutex> lock(coverageMutex);covered.invalidate();}
bool storage_file_layout(const std::string& path,const pro::incremental::Stamp* previous,StorageFileLayout& out,bool(*cancelled)()){
 std::lock_guard<std::mutex> lock(coverageMutex);
 if((cancelled&&cancelled())||!mappingAllowed||!storage_ready())return false;
 const uint32_t first=locations.count(path)?locations[path]:100;
 out.size=fake::data.size();out.stamp=covered.stamp(storage_generation());
 assert(pro::incremental::digest(hash_text("layout/"+std::to_string(first)+"/"+std::to_string(out.size)),out.digest));
 out.clean=!previous||covered.clean(*previous,storage_generation(),first,1);return true;
}
static void changed(uint32_t first){std::lock_guard<std::mutex> lock(coverageMutex);covered.wrote(++fake::epoch,first,first);fake::viewEpoch=fake::epoch.load();}
int main(int argc,char** argv){
 assert(argc==3);sidecar=argv[2];std::array<uint8_t,16> boot{};boot[0]=1;covered.start(boot);
 const std::string scenario=argv[1];
 if(scenario=="worker-restart"||scenario=="worker-audit"||scenario=="worker-audit-not-postponed"){
   fake::wifi=WL_CONNECTED;fake::allowSnapshot=true;fake::capacityOk=true;
   assert(backup_worker_start(cfg()));idle();assert(fake::fingerprints==1);
   if(scenario=="worker-restart"){
     retire();assert(backup_worker_start(cfg()));idle();assert(fake::fingerprints==1);
     changed(9999);until([]{return fake::mounts.load()>=2;});idle();assert(fake::fingerprints==1);retire();
   }else if(scenario=="worker-audit-not-postponed"){
     fake::clock=incrementalAuditedAt+pro::incremental::AuditMs-60000;
     changed(9999);until([]{return fake::mounts.load()>=2;});idle();assert(fake::fingerprints==1);
     fake::clock.fetch_add(60001);until([]{return fake::fingerprints.load()>=2;});idle();assert(fake::fingerprints==2);retire();
   }else{
     fake::clock.fetch_add(pro::incremental::AuditMs);until([]{return fake::fingerprints.load()>=2;});idle();assert(fake::fingerprints==2);retire();
   }
   std::cout<<"PASS actual worker "<<scenario<<"\n";return 0;
 }
 fake::snapshotOpen=true;fake::viewEpoch=fake::epoch.load();fake::allowSnapshot=true;
 DeviceLocal local;local.configure(scan_scope(cfg()));pro::FileJournal journal("SYNTHETIC-IN-MEMORY");WebDAVClient remote(cfg());
 pro::Backup engine(local,journal,remote,hash_text(cfg().server_url+"\n"+cfg().username));
 auto step=[&](const std::string& path){return engine.process(path,1767225600);};
 if(scenario=="hundred-plus-two"){
   for(unsigned i=0;i<100;++i){const auto path="QA/original-"+std::to_string(i)+".pdf";locations[path]=100+i;assert(step(path).outcome==pro::Outcome::Verified);}
   assert(fake::fingerprints==100&&sidecarWrites==100);local.forceHash=false;covered.seal(storage_generation());
   const auto start=std::chrono::steady_clock::now();const auto before=sidecarReadBytes;
   for(unsigned addition=0;addition<2;++addition){
     const auto path="QA/added-"+std::to_string(addition)+".pdf";locations[path]=200+addition;changed(200+addition);
     const auto hashes=fake::fingerprints.load();const auto writes=sidecarWrites;
     for(unsigned i=0;i<100;++i)assert(step("QA/original-"+std::to_string(i)+".pdf").outcome==pro::Outcome::Verified);
     for(unsigned prior=0;prior<addition;++prior)assert(step("QA/added-"+std::to_string(prior)+".pdf").outcome==pro::Outcome::Verified);
     assert(fake::fingerprints==hashes&&sidecarWrites==writes);
     assert(step(path).outcome==pro::Outcome::Verified&&fake::fingerprints==hashes+1);covered.seal(storage_generation());
   }
   const auto micros=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
   assert(fake::fingerprints==102&&sidecarWrites==102);fake::snapshotOpen=false;
   std::cout<<"PASS100 verified+2 adjacent new cohorts: old content hashes=0; new hashes=2; hit sidecar writes=0; sidecar_read_bytes="<<sidecarReadBytes-before<<" native_fixture_elapsed_us="<<micros<<"\n";return 0;
 }
 locations["QA/old.pdf"]=100;locations["QA/new.pdf"]=101;
 assert(step("QA/old.pdf").outcome==pro::Outcome::Verified);assert(fake::fingerprints==1&&fake::puts==1);local.forceHash=false;covered.seal(storage_generation());
 assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==1);
 if(scenario=="new-file"){
   // Metadata/new-file sectors change; no old file data sectors were written.
   for(unsigned i=0;i<12;++i){const std::string path="QA/new-"+std::to_string(i)+".pdf";locations[path]=101+i;changed(101+i);
     const auto reads=fake::fingerprints.load();assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==reads);
     assert(step(path).outcome==pro::Outcome::Verified&&fake::fingerprints==reads+1);covered.seal(storage_generation());}
 }else if(scenario=="same-size-preserved-time"){
   fake::data="SYNTHETIC B";changed(100);assert(step("QA/old.pdf").outcome==pro::Outcome::Verified);assert(fake::fingerprints==2&&fake::puts==2);
 }else if(scenario=="overflow"){
   for(unsigned i=0;i<65;++i){changed(1000+i*2);covered.seal(storage_generation());}
   assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==2&&fake::puts==1);
 }else if(scenario=="receipt"){
   ++pro::receipts[hash_text("QA/old.pdf")].sequence;
   assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==2);
 }else if(scenario=="corrupt"||scenario=="missing"){
   if(scenario=="missing")assert(!remove(sidecar.c_str()));else{FILE* f=fopen(sidecar.c_str(),"wb");assert(f);fputc('x',f);assert(!fclose(f));}
   assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==2);
 }else if(scenario=="mapping-fallback"){
   mappingAllowed=false;assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==2);
 }else if(scenario=="interrupted"){
   changed(100);fake::onFingerprint=[]{++fake::epoch;};
   assert(step("QA/old.pdf").outcome==pro::Outcome::LocalError);fake::onFingerprint={};covered.invalidate();fake::viewEpoch=fake::epoch.load();
   assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==3);
 }else if(scenario=="reboot"){
   boot[0]=2;covered.start(boot);assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==2);
 }else if(scenario=="audit"){
   fake::clock=pro::incremental::AuditMs;assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==2);
 }else if(scenario=="scope"){
   local.configure(hash_text("different complete config"));assert(step("QA/old.pdf").outcome==pro::Outcome::Verified&&fake::fingerprints==2);
 }else if(scenario=="pending-not-reused"){
   auto& record=pro::receipts[hash_text("QA/old.pdf")];record.state=pro::State::Pending;record.next=1767225610;
   assert(step("QA/old.pdf").outcome==pro::Outcome::Pending&&fake::fingerprints==2);
 }else assert(false&&"unknown scenario");
 fake::snapshotOpen=false;
 std::cout<<"PASS actual Backup+DeviceLocal: "<<scenario<<" fingerprints="<<fake::fingerprints<<" puts="<<fake::puts<<"\n";
}
