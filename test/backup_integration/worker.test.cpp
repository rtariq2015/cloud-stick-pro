#include "stubs/fakes.h"
#include "backup_worker.h"
#include <iostream>

namespace {
unsigned cases=0;
template<class Predicate>void until(Predicate ready){
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  while(!ready()){
    assert(std::chrono::steady_clock::now()<end&&"worker handoff timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
CloudConfig fixture(const char* folder="SYNTHETIC_QA"){
  CloudConfig c;c.is_valid=true;c.server_url="https://synthetic.invalid/dav";c.username="synthetic";
  c.password="test-only";c.ca_pem="synthetic-ca";c.watch_folder=folder;return c;
}
void reset(){
  assert(backup_worker_snapshot().phase==BackupWorkerPhase::Stopped);
  assert(fake::tasks.empty()&&fake::allocations.empty());
  fake::events.clear();fake::onEvent={};fake::duringCreate={};fake::holdLaunch=false;fake::holdPark=false;
  fake::holdBegin=false;fake::holdCheck=false;fake::beginEntered=false;fake::checkEntered=false;
  fake::allowSnapshot=false;fake::snapshotOpen=false;fake::snapshotEntered=false;fake::holdSnapshot=false;
  fake::wifi=0;fake::beginCalls=0;fake::checkCalls=0;fake::hideSuspended=false;fake::failAllocation=-1;
  set_maintenance(true);
}
void reap(){until([]{return backup_worker_reap();});assert(backup_worker_snapshot().phase==BackupWorkerPhase::Stopped);}
void retire(){
  const auto generation=backup_worker_request_retire();assert(generation);
  assert(backup_worker_request_retire()==generation);
  until([&]{const auto s=backup_worker_snapshot();return s.phase==BackupWorkerPhase::Drained&&s.retired_generation==generation;});
  reap();assert(maintenance_active());assert(backup_folder()=="QA");
  assert(fake::tasks.empty()&&fake::allocations.empty());
}
void pass(const char* name){++cases;std::cout<<"PASS "<<name<<'\n';}
}
int main(int argc,char** argv){
  if(argc==2){
    reset();const std::string mode=argv[1];
    const bool owned=mode=="fs-end-fail";
    if(mode=="fs-preexisting")fake::metadataMounted=true;
    else if(mode=="fs-partial"){fake::beginResult=false;fake::partialMount=true;}
    else if(mode=="fs-begin-fail")fake::beginResult=false;
    else assert(owned);
    if(owned)fake::endFails=true;
    assert(backup_worker_start(fixture()));
    if(owned||mode=="fs-begin-fail"){
      until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Running;});
      backup_worker_request_retire();
    }
    until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});
    if(mode=="fs-begin-fail"){
      reap();assert(fake::endCalls==0&&!fake::metadataMounted);
      pass("failed unmounted init needs no unmount; clean retirement remains possible");return 0;
    }
    assert(backup_worker_snapshot().error==-2115&&maintenance_active());
    assert(!backup_worker_reap()&&!backup_worker_start(fixture()));
    assert(fake::metadataMounted&&fake::endCalls==unsigned(owned));
    assert(fake::allocations.size()==2&&fake::tasks.size()==1);
    // The production contract deliberately retains ownership after uncertain
    // cleanup. Tear down only the simulated board's terminal RAM so this test
    // process can exit; this is not a production recovery API or forced stop.
    auto handle=*fake::tasks.begin();until([&]{return handle->state==eSuspended;});
    vTaskDelete(handle);
    while(!fake::allocations.empty())heap_caps_free(*fake::allocations.begin());
    pass("metadata cleanup failure remains blocked without false reap/resume");return 0;
  }
  reset();CloudConfig invalid;
  assert(!backup_worker_start(invalid));assert(backup_worker_snapshot().generation==0);
  pass("invalid config admits no task");

  fake::owner.core=-1;assert(!backup_worker_start(fixture()));fake::owner.core=1;
  assert(backup_worker_snapshot().phase==BackupWorkerPhase::Stopped);
  pass("unpinned owner refused");

  for(int fail=0;fail<2;++fail){
    reset();fake::failAllocation=fail;
    assert(!backup_worker_start(fixture()));assert(maintenance_active());
    assert(backup_worker_snapshot().phase==BackupWorkerPhase::Failed);
    assert(fake::allocations.empty()&&fake::tasks.empty());
    assert(!backup_worker_start(fixture("REPLACEMENT")));reap();assert(backup_folder()=="QA");
  }
  pass("stack and TCB allocation failures release partial allocations");

  reset();fake::failCreate=true;
  assert(!backup_worker_start(fixture()));assert(maintenance_active());
  assert(backup_worker_snapshot().phase==BackupWorkerPhase::Failed);
  assert(fake::allocations.empty());reap();assert(backup_folder()=="QA");
  pass("RTOS creation failure is explicit and reap releases config");

  reset();fake::holdLaunch=true;
  assert(backup_worker_start(fixture()));assert(!maintenance_active());
  const auto first=backup_worker_snapshot().generation;
  assert(!backup_worker_start(fixture("WRONG_DESTINATION")));
  assert(backup_folder()=="SYNTHETIC_QA"&&backup_worker_snapshot().generation==first);
  set_maintenance(true);fake::release(fake::holdLaunch);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(fake::beginCalls==0);
  retire();assert(fake::beginCalls==0&&fake::checkCalls==0);
  pass("duplicate start preserves config; pause/retire before init opens nothing");

  reset();fake::duringCreate=[]{
    assert(backup_worker_snapshot().phase==BackupWorkerPhase::Starting);
    assert(backup_worker_request_retire()!=0);
    // Give the fresh task time to attempt its pre-publication handoff.
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    assert(fake::beginCalls==0&&fake::checkCalls==0);
  };
  assert(backup_worker_start(fixture()));assert(maintenance_active());
  until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});reap();
  assert(fake::beginCalls==0&&fake::checkCalls==0);
  pass("retirement during task-create publication cannot start work");

  reset();fake::failCreate=true;fake::duringCreate=[]{
    assert(backup_worker_request_retire()!=0);
    assert(backup_worker_snapshot().phase==BackupWorkerPhase::Retiring);
  };
  assert(!backup_worker_start(fixture()));assert(maintenance_active());
  assert(backup_worker_snapshot().phase==BackupWorkerPhase::Failed);
  assert(fake::tasks.empty()&&fake::allocations.empty());reap();assert(backup_folder()=="QA");
  pass("retirement concurrent with failed creation cannot publish Running or leak buffers");

  reset();fake::holdBegin=true;
  assert(backup_worker_start(fixture()));until([]{return fake::beginEntered.load();});
  auto generation=backup_worker_request_retire();assert(generation);
  assert(backup_worker_snapshot().phase==BackupWorkerPhase::Retiring&&!backup_worker_reap());
  fake::release(fake::holdBegin);
  until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});
  reap();assert(fake::checkCalls==0);
  pass("retirement while journal init blocks waits and cancels remaining init");

  reset();fake::wifi=WL_CONNECTED;fake::holdCheck=true;fake::holdPark=true;
  std::atomic<unsigned> cleanupBeforeAck{0};
  fake::onEvent=[&](const std::string& event){
    if(event=="TLS destroyed"||event=="engine destroyed"||event=="remote destroyed"||event=="journal destroyed"){
      assert(backup_worker_snapshot().phase==BackupWorkerPhase::Retiring);++cleanupBeforeAck;
    }
  };
  assert(backup_worker_start(fixture()));until([]{return fake::checkEntered.load();});
  assert(backup_status().active);
  generation=backup_worker_request_retire();
  assert(!request_connection_check());assert(!backup_worker_reap());
  assert(backup_worker_snapshot().phase==BackupWorkerPhase::Retiring&&cleanupBeforeAck==0);
  assert(!backup_worker_start(fixture("WRONG_DESTINATION")));
  assert(backup_folder()=="SYNTHETIC_QA");
  fake::release(fake::holdCheck);
  until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});
  assert(cleanupBeforeAck==4&&!backup_status().active);
  assert(fake::endCalls>0&&!fake::metadataMounted);
  assert(backup_worker_snapshot().retired_generation==generation);
  assert(!backup_worker_reap()); // Drained precedes the actual suspension proof.
  assert(fake::allocations.size()==2&&backup_folder()=="SYNTHETIC_QA");
  fake::hideSuspended=true;fake::release(fake::holdPark);
  assert(!backup_worker_reap()); // Scheduler state must prove parked too.
  fake::hideSuspended=false;
  FakeTask stranger;fake::current=&stranger;
  assert(!backup_worker_start(fixture())&&!backup_worker_reap()&&backup_worker_request_retire()==0);
  fake::current=&fake::owner;
  fake::actualCore=0;assert(!backup_worker_reap());fake::actualCore=1;
  reap();assert(backup_folder()=="QA"&&fake::allocations.empty()&&fake::tasks.empty());
  const auto queryCount=fake::queries.load();assert(backup_worker_reap());
  (void)backup_worker_snapshot();assert(fake::queries==queryCount); // No freed-handle queries.
  pass("TLS/RAII drain before ack; owner/core/park proofs before freeing; no stale handle query");

  reset();fake::wifi=WL_CONNECTED;fake::allowSnapshot=true;fake::holdSnapshot=true;
  assert(backup_worker_start(fixture()));until([]{return fake::snapshotEntered.load();});
  assert(backup_status().active&&fake::snapshotOpen);
  assert(backup_worker_request_retire());assert(!backup_worker_reap());
  fake::release(fake::holdSnapshot);
  until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});
  assert(!fake::snapshotOpen&&!backup_status().active);
  {
    std::lock_guard<std::mutex> lock(fake::mutex);
    const auto released=std::find(fake::events.begin(),fake::events.end(),"snapshot released");
    const auto destroyed=std::find(fake::events.begin(),fake::events.end(),"engine destroyed");
    assert(released!=fake::events.end()&&released<destroyed);
  }
  reap();pass("worker releases read snapshot before RAII drain and acknowledgment");

  for(unsigned cycle=0;cycle<20;++cycle){
    reset();const auto previous=backup_worker_snapshot().generation;
    assert(backup_worker_start(fixture()));
    until([]{return backup_worker_snapshot().phase==BackupWorkerPhase::Running;});
    assert(backup_worker_snapshot().generation==previous+1);
    assert(fake::remoteIdentity=="https://synthetic.invalid/dav");retire();
  }
  pass("20 repeated start/drain/reap cycles preserve destination and reclaim buffers");
  assert(fake::allocations.empty()&&fake::tasks.empty());
  std::cout<<cases<<" actual adapter cases passed; all data synthetic; no hardware/network/storage writes\n";
}
