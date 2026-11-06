#include "fakes.h"
#include "remote_health_worker.h"
#include <ctime>
#include <iostream>
namespace {
BackupStatus observed;ConnectionCheck checked;bool paused=false;
time_t clockUtc=1767226600;
pro::health::Snapshot last(){std::lock_guard<std::mutex> lock(fake::mutex);return fake::healthSnapshots.back();}
}
extern "C" time_t __wrap_time(time_t* out){if(out)*out=clockUtc;return clockUtc;}
BackupStatus backup_status(){return observed;}
ConnectionCheck connection_check(){return checked;}
bool maintenance_active(){return paused;}
int main(){
  using namespace pro::health;
  fake::wifi=WL_CONNECTED;fake::cardInitialized=true;checked.state=pro::CheckState::Ready;
  checked.code=207;checked.checkedAt=uint64_t(clockUtc);observed.hasScan=true;observed.verified=99;observed.lastBackup=uint64_t(clockUtc)-100;
  CloudConfig config;WebDAVClient remote(config);WorkerReporter first;
  first.tick(remote,false);paused=true;first.tick(remote,true);paused=false;
  observed.active=true;first.tick(remote,true);observed.active=false;
  observed.scanning=true;first.tick(remote,true);observed.scanning=false;
  checked.state=pro::CheckState::Checking;first.tick(remote,true);checked.state=pro::CheckState::Ready;
  fake::snapshotOpen=true;first.tick(remote,true);fake::snapshotOpen=false;
  fake::wifi=0;first.tick(remote,true);fake::wifi=WL_CONNECTED;
  clockUtc=0;first.tick(remote,true);clockUtc=1767226600;
  assert(fake::healthCalls==0);
  first.tick(remote,true);assert(fake::healthCalls==1);
  auto initial=last();assert(initial.scan==Scan::Unknown&&initial.verified==0&&initial.scan_observed_at==0);
  assert(!initial.capacity_known&&initial.last_file_verified_at==observed.lastBackup);
  assert(initial.cloud_check==CloudCheck::ReadAccessOk&&initial.cloud_checked_at==checked.checkedAt);
  const auto boot=initial.boot_id;
  WorkerReporter collector;collector.scanStarted();clockUtc+=10;collector.capacityMeasured(true,4096,1024);
  const auto measured=clockUtc;clockUtc+=10;observed.pending=2;observed.failed=1;observed.verified=3;observed.scanComplete=false;
  collector.scanProgress(observed);clockUtc+=10;collector.scanEnded(observed,false);const auto ended=clockUtc;
  clockUtc+=100;collector.tick(remote,true);auto partial=last();
  assert(partial.boot_id==boot&&partial.scan==Scan::Partial&&partial.pending==2&&partial.failed==1&&partial.verified==3);
  assert(partial.scan_observed_at==uint64_t(ended)&&!partial.capacity_known);
  // Promote capacity only after a stable completed source view, retaining the
  // measurement's own timestamp rather than the later publication time.
  WorkerReporter complete;complete.scanStarted();clockUtc+=10;complete.capacityMeasured(true,8192,2048);const auto actualMeasurement=clockUtc;
  observed.scanComplete=true;clockUtc+=10;complete.scanEnded(observed,true);clockUtc+=50;
  complete.tick(remote,true);auto finished=last();assert(finished.scan==Scan::Complete&&finished.capacity_known);
  assert(finished.capacity_total==8192&&finished.capacity_free==2048&&finished.capacity_observed_at==uint64_t(actualMeasurement));
  assert(finished.capacity_observed_at!=finished.reported_at&&uint64_t(measured)<finished.capacity_observed_at);
  // A subsequent failed measurement cannot fabricate new capacity or freshness.
  complete.scanStarted();clockUtc+=10;complete.capacityMeasured(false,9999,8888);observed.scanComplete=false;
  complete.scanEnded(observed,false);fake::clock.fetch_add(Schedule::SuccessMs+1);complete.tick(remote,true);
  const auto cached=last();assert(cached.capacity_total==8192&&cached.capacity_observed_at==uint64_t(actualMeasurement));
  assert(cached.scan==Scan::Partial&&!cached.read_view_active);
  WorkerReporter noClock;clockUtc=0;noClock.scanStarted();noClock.capacityMeasured(true,4096,1024);noClock.scanEnded(observed,true);
  clockUtc=1767226600;noClock.tick(remote,true);const auto unknownTime=last();
  assert(unknownTime.scan_observed_at==0&&unknownTime.capacity_observed_at==0);
  assert(unknownTime.sd==Sd::Initialized); // Unmounted read view is not SD failure.
  std::cout<<"PASS actual worker observer timestamps, unknowns and priority admission\n";
}
