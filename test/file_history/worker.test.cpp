#include "../backup_integration/stubs/fakes.h"
#include "backup_worker.h"
#include "file_history.h"
#include "pro_event_log.h"
#if CSP_ENABLE_EVENT_LOG
#include "../event_log/io.h"
#include <filesystem>
#endif
#include <iostream>
static std::atomic<unsigned> captures{0},releases{0};static std::atomic<bool> cache{false};
namespace pro::history {
bool capture(FileJournal&,const std::string& dest,const std::string& folder,uint64_t){
 assert(fake::metadataMounted&&fake::journalLive==1&&fake::remoteLive==0&&fake::engineLive==0&&!fake::snapshotOpen);
 assert(!(fake::holdCheck&&fake::checkEntered));assert(maintenance_active());
#if CSP_ENABLE_EVENT_LOG
 assert(event_io::paths.empty()&&!pro::events::status().dirty);
#endif
 assert(dest=="https://synthetic.invalid/\nowner"&&folder=="QA");++captures;cache=true;fake::event("history captured");return true;
}
void release(){assert(fake::current==&fake::owner);++releases;cache=false;fake::event("history released");}
}
template<class P>void until(P p){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!p()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
static CloudConfig config(){CloudConfig c;c.is_valid=true;c.server_url="https://synthetic.invalid/";c.username="owner";return c;}
static void reap(){until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});until([]{return backup_worker_reap();});assert(fake::tasks.empty()&&fake::allocations.empty()&&!fake::metadataMounted);}
int main(int argc,char** argv){
 assert(argc==2);const std::string mode=argv[1];set_maintenance(true);
#if CSP_ENABLE_EVENT_LOG
 std::filesystem::create_directories(CSP_EVENT_LOG_ROOT);
#endif
 fake::onEvent=[](const std::string& e){if(e=="remote created"||e=="engine created")assert(!cache);};
 if(mode=="mount-fail")fake::beginResult=false;
 else if(mode=="tls"){fake::wifi=WL_CONNECTED;fake::holdCheck=true;}
 else if(mode=="snapshot"){fake::wifi=WL_CONNECTED;fake::allowSnapshot=true;fake::holdSnapshot=true;}
 else assert(mode=="normal"||mode=="restart-fail");
 assert(backup_worker_start(config()));
 if(mode=="mount-fail"){until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Running;});assert(backup_worker_request_retire());reap();assert(captures==0&&releases==1);}
 else{
  if(mode=="tls")until([]{return fake::checkEntered.load();});
  else if(mode=="snapshot")until([]{return fake::snapshotEntered.load();});
  else until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Running;});
  assert(captures==0&&!cache);assert(backup_worker_request_retire());
  if(mode=="tls")fake::release(fake::holdCheck);if(mode=="snapshot")fake::release(fake::holdSnapshot);reap();
  assert(captures==1&&cache&&releases==1);
  if(mode=="restart-fail"){
   fake::failAllocation=0;assert(!backup_worker_start(config()));assert(!cache&&releases==2&&captures==1);
   assert(fake::tasks.empty()&&fake::allocations.empty());
  }else{
   assert(backup_worker_start(config()));until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Running;});
   assert(!cache&&releases==2&&captures==1);assert(backup_worker_request_retire());reap();assert(captures==2&&cache);
  }
 }
 pro::history::release();assert(!cache);
 std::cout<<"PASS actual worker retirement/history ordering "<<mode<<"; no TLS/engine/snapshot overlap, release before restart\n";
}
