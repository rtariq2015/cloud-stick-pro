// Actual managed worker + observer; fake RTOS/storage/DAV, no real files/TLS.
#include "fakes.h"
#include "backup_worker.h"
#include <iostream>
#include <ctime>
namespace {
template<class P>void until(P ready){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  while(!ready()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
CloudConfig config(){CloudConfig c;c.is_valid=true;c.watch_folder="SYNTHETIC_HEALTH_MISSING_FOLDER";
  c.server_url="https://synthetic.invalid/";c.username="synthetic";c.password="synthetic";c.ca_pem="synthetic";
  c.sync_interval_s=1000000;return c;}
void retire(){const auto generation=backup_worker_request_retire();
  until([&]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained;});
  assert(backup_worker_snapshot().retired_generation==generation);
  until([]{return backup_worker_reap();});assert(backup_worker_snapshot().phase==BackupWorkerPhase::Stopped);
  assert(fake::allocations.empty()&&fake::tasks.empty());}
pro::health::Snapshot report(size_t index){std::lock_guard<std::mutex> lock(fake::mutex);return fake::healthSnapshots.at(index);}
}
int main(int argc,char** argv){
  assert(argc==2);const std::string mode=argv[1];fake::wifi=WL_CONNECTED;fake::allowSnapshot=true;
  fake::capacityOk=true;fake::holdSnapshot=mode=="snapshot";fake::holdHealth=mode=="retire-health";
  if(mode=="failure")fake::healthCode=507;
  assert(backup_worker_start(config()));
  if(mode=="snapshot"){
    until([]{return fake::snapshotEntered.load();});assert(fake::snapshotOpen&&fake::healthCalls==0);
    fake::release(fake::holdSnapshot);
  }
  until([]{return fake::healthCalls.load()==1;});
  const auto first=report(0);assert(!first.read_view_active&&first.sd==pro::health::Sd::Initialized);
  assert(first.scan==pro::health::Scan::Partial&&first.scan_observed_at>=1767225600ULL);
  assert(first.capacity_known&&first.capacity_total==4096&&first.capacity_free==1024);
  assert(first.capacity_observed_at>=1767225600ULL&&first.wifi==pro::health::Wifi::Connected);
  assert(first.last_file_verified_at==0&&first.pending==0&&first.verified==0);
  if(mode=="retire-health"){
    const auto generation=backup_worker_request_retire();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(backup_worker_snapshot().phase==BackupWorkerPhase::Retiring);
    assert(!fake::snapshotOpen&&fake::healthDone==0);
    fake::release(fake::holdHealth);
    until([&]{return backup_worker_snapshot().phase==BackupWorkerPhase::Drained&&backup_worker_snapshot().retired_generation==generation;});
    until([]{return backup_worker_reap();});
  }else if(mode=="failure"){
    until([]{return fake::healthDone.load()==1;});
    const auto before=backup_status();assert(before.error==-2102&&!before.active);
    // A health failure neither changes this file/scan error nor clears receipts.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto after=backup_status();assert(after.error==before.error&&after.verified==before.verified&&after.lastBackup==before.lastBackup);
    assert(fake::healthCalls==1);backup_message("cleared synthetic backup error",0);
    fake::clock.fetch_add(pro::health::Schedule::FirstRetryMs+1);fake::healthCode=200;
    until([]{return fake::healthCalls.load()==2;});
    const auto retry=report(1);assert(retry.error_subsystem==pro::health::Subsystem::Health&&retry.error_code==507);
    retire();
  }else{assert(mode=="snapshot"||mode=="normal");retire();}
  assert(!fake::snapshotOpen&&!fake::metadataMounted&&fake::tasks.empty()&&fake::allocations.empty());
  std::cout<<"PASS managed worker health boundary and independent failure handling\n";
}
