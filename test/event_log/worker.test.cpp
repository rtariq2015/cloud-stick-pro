#include "../backup_integration/stubs/fakes.h"
#include "backup_worker.h"
#include "pro_event_log.h"
#include "io.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
template<class Predicate>void until(Predicate predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!predicate()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
CloudConfig config(){CloudConfig c;c.is_valid=true;c.server_url="https://SYNTHETIC-DO-NOT-LOG.invalid/private";c.username="PRIVATE_USER";c.password="PRIVATE_PASSWORD";c.watch_folder="PRIVATE_FILENAME";return c;}
void reap(){until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});until([]{return backup_worker_reap();});assert(fake::tasks.empty()&&fake::allocations.empty()&&!fake::metadataMounted);}
void retire(){assert(backup_worker_request_retire());reap();}
void beginIo(){
 event_io::before=[](const char*){
  assert(fake::current!=&fake::owner);assert(fake::metadataMounted);assert(!fake::snapshotOpen);
  assert(!(fake::holdCheck&&fake::checkEntered));
 };
}
int main(int argc,char** argv){
 assert(argc==2);const std::string mode=argv[1];
 fs::remove_all(CSP_EVENT_LOG_ROOT);fs::create_directories(std::string(CSP_EVENT_LOG_ROOT)+"/queue");
 {std::ofstream receipt(std::string(CSP_EVENT_LOG_ROOT)+"/queue/receipt.fixture");receipt<<"UNCHANGED_RECEIPT";}
 set_maintenance(true);beginIo();
 if(mode=="tls"){
  fake::wifi=WL_CONNECTED;fake::holdCheck=true;
  assert(backup_worker_start(config()));until([]{return fake::checkEntered.load();});
  const auto writes=event_io::writes,opens=event_io::opens;std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(writes==event_io::writes&&opens==event_io::opens);
  assert(backup_worker_request_retire());fake::release(fake::holdCheck);reap();
#if CSP_ENABLE_EVENT_LOG
  assert(pro::events::status().loaded&&pro::events::status().count>=3);assert(!pro::events::status().dirty&&event_io::writes>=2);
  const auto page=pro::events::readPage();assert(page.count);const auto oldOpens=event_io::opens;(void)pro::events::readPage(page.next);assert(event_io::opens==oldOpens);
#endif
 }else if(mode=="snapshot"){
  fake::wifi=WL_CONNECTED;fake::allowSnapshot=true;fake::holdSnapshot=true;
  assert(backup_worker_start(config()));until([]{return fake::snapshotEntered.load();});const auto writes=event_io::writes,opens=event_io::opens;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(writes==event_io::writes&&opens==event_io::opens);
  assert(backup_worker_request_retire());fake::release(fake::holdSnapshot);reap();
#if CSP_ENABLE_EVENT_LOG
  bool partial=false,verified=false;uint32_t next=0;do{const auto page=pro::events::readPage(next);for(unsigned i=0;i<page.count;++i){const auto& e=page.events[i];if(e.kind==pro::events::Kind::ScanOutcome)partial=e.code==-2010;if(e.kind==pro::events::Kind::VerifiedCount)verified=e.code==0;}if(!page.more)break;next=page.next;}while(true);
  assert(partial&&verified);assert(event_io::writes>=2);
#endif
 }else if(mode=="io-failure"){
  event_io::failure="write";assert(backup_worker_start(config()));until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Running;});
  std::this_thread::sleep_for(std::chrono::milliseconds(30));retire();
#if CSP_ENABLE_EVENT_LOG
  assert(pro::events::status().io_failed&&pro::events::status().dirty);assert(event_io::writes<=3);
#endif
  assert(backup_worker_snapshot().error==0&&maintenance_active());
 }else if(mode=="coalesce"){
  assert(backup_worker_start(config()));until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Running;});
  std::this_thread::sleep_for(std::chrono::milliseconds(50));retire();
#if CSP_ENABLE_EVENT_LOG
  assert(event_io::writes<=3&&event_io::writes>=2);
#endif
 }else assert(false);
 assert(event_io::paths.empty());std::ifstream receipt(std::string(CSP_EVENT_LOG_ROOT)+"/queue/receipt.fixture");std::string unchanged;receipt>>unchanged;assert(unchanged=="UNCHANGED_RECEIPT");
#if CSP_ENABLE_EVENT_LOG
 const auto saved=std::filesystem::path(CSP_EVENT_LOG_ROOT)/"events.log";if(fs::exists(saved)){assert(fs::file_size(saved)<=pro::events::MaximumSnapshotBytes);std::ifstream input(saved,std::ios::binary);std::string content{std::istreambuf_iterator<char>(input),{}};for(const char* privateValue:{"PRIVATE_USER","PRIVATE_PASSWORD","PRIVATE_FILENAME","SYNTHETIC-DO-NOT-LOG"})assert(content.find(privateValue)==std::string::npos);}
#else
 assert(event_io::opens==0&&event_io::writes==0&&event_io::renames==0);assert(pro::events::readPage().count==0&&!pro::events::status().loaded);
#endif
 std::cout<<"PASS actual worker "<<mode<<" event_log="<<CSP_ENABLE_EVENT_LOG<<"; metadata owner/TLS/snapshot/reap/receipt invariants hold\n";
}
