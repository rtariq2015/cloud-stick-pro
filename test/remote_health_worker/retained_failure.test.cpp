// Actual WorkerReporter/Schedule; synthetic publisher failure and UTC/monotonic.
#include "fakes.h"
#include "remote_health_worker.h"
#include <ctime>
#include <iostream>
namespace {BackupStatus observed;ConnectionCheck checked;time_t clockUtc=1767226600;}
extern "C" time_t __wrap_time(time_t* out){if(out)*out=clockUtc;return clockUtc;}
BackupStatus backup_status(){return observed;}
ConnectionCheck connection_check(){return checked;}
bool maintenance_active(){return false;}
int main(){
  using namespace pro::health;
  fake::wifi=WL_CONNECTED;fake::healthCode=HealthCollision;fake::cardInitialized=true;
  checked.state=pro::CheckState::Verified;checked.code=200;checked.checkedAt=uint64_t(clockUtc);
  observed.verified=10;observed.lastBackup=uint64_t(clockUtc)-100;
  CloudConfig config;WebDAVClient remote(config);WorkerReporter retained;
  retained.tick(remote,true);assert(fake::healthCalls==1);
  const uint32_t delays[]={Schedule::FirstRetryMs,600000,1200000,2400000,Schedule::MaxRetryMs};
  for(unsigned i=0;i<5;++i){
    const auto before=fake::healthCalls.load();
    fake::clock.fetch_add(delays[i]-1);clockUtc+=delays[i]/1000;
    // Same object retained through a worker stop/start preserves error/backoff.
    auto& afterRestart=retained;afterRestart.tick(remote,true);
    assert(fake::healthCalls==before);
    fake::clock.fetch_add(1);afterRestart.tick(remote,true);
    assert(fake::healthCalls==before+1);
    const auto& last=fake::healthSnapshots.back();
    assert(last.error_subsystem==Subsystem::Health&&last.error_code==HealthCollision);
    assert(last.cloud_check==CloudCheck::UploadReadbackOk&&last.last_file_verified_at==observed.lastBackup);
    assert(observed.verified==10&&observed.error==0);
  }
  WorkerReporter fresh;fresh.tick(remote,true);
  assert(fake::healthCalls==7); // Fresh reporter differs from retained same-boot state.
  fake::healthCode=200;
  // Recovery must wait for the existing backoff; success resets subsequent
  // scheduling and clears internal health error, without rewriting history.
  const auto before=fake::healthCalls.load();retained.tick(remote,true);assert(fake::healthCalls==before);
  fake::clock.fetch_add(Schedule::MaxRetryMs);clockUtc+=Schedule::MaxRetryMs/1000;
  retained.tick(remote,true);assert(fake::healthCalls==before+1);
  assert(fake::healthSnapshots.back().error_code==HealthCollision); // Snapshot predates acknowledgment.
  fake::clock.fetch_add(Schedule::SuccessMs-1);retained.tick(remote,true);assert(fake::healthCalls==before+1);
  fake::clock.fetch_add(1);clockUtc+=Schedule::SuccessMs/1000;retained.tick(remote,true);
  assert(fake::healthCalls==before+2&&fake::healthSnapshots.back().error_subsystem==Subsystem::None);
  assert(fake::healthSnapshots.back().error_code==0&&fake::healthSnapshots.back().error_observed_at==0);
  assert(observed.verified==10&&observed.lastBackup==1767226500&&observed.error==0);
  std::cout<<"PASS actual retained reporter retries300/600/1200/2400/3600seconds; backup facts unchanged\n";
  std::cout<<"PASS success respects backoff then clears health error for next15minute report\n";
}
