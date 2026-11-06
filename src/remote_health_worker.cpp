#include "remote_health_worker.h"
#if CSP_ENABLE_REMOTE_HEALTH
#include "webdav_client.h"
#include "pro_device.h"
#include "usb_storage.h"
#include <WiFi.h>
#include <esp_random.h>
#include <ctime>
#include <cstdio>
namespace pro::health {
namespace {
// Eight bytes, not a resident report buffer. The managed worker is serialized
// across starts/reaps, so this identifier remains stable through BLE windows.
uint64_t boot_identifier=0;
uint64_t utc(){const auto now=time(nullptr);return now>=1767225600LL&&uint64_t(now)<=253402300799ULL?uint64_t(now):0;}
void counts(Snapshot& s,const BackupStatus& b){s.pending=b.pending;s.failed=b.failed;s.verified=b.verified;s.skipped=b.skipped;}
}
WorkerReporter::WorkerReporter(){
  if(!boot_identifier){boot_identifier=(uint64_t(esp_random())<<32)|esp_random();if(!boot_identifier)boot_identifier=1;}
  observed_.boot_id=boot_identifier;
  std::snprintf(observed_.firmware,sizeof observed_.firmware,"%s",FirmwareVersion);
}
void WorkerReporter::scanStarted(){
  observed_.scan=Scan::Partial;observed_.scan_observed_at=utc();
  observed_.pending=observed_.failed=observed_.verified=observed_.skipped=0;staged_capacity_=false;
}
void WorkerReporter::scanProgress(const BackupStatus& b){
  observed_.scan=Scan::Partial;observed_.scan_observed_at=utc();counts(observed_,b);
  if(b.error!=last_backup_error_){last_backup_error_=b.error;backup_error_at_=b.error?utc():0;}
}
void WorkerReporter::capacityMeasured(bool known,uint64_t total,uint64_t free){
  staged_capacity_=known&&total&&free<=total;staged_total_=total;staged_free_=free;staged_at_=utc();
}
void WorkerReporter::scanEnded(const BackupStatus& b,bool consistent){
  scanProgress(b);observed_.scan=b.scanComplete?Scan::Complete:Scan::Partial;
  if(staged_capacity_&&consistent){observed_.capacity_known=true;observed_.capacity_total=staged_total_;
    observed_.capacity_free=staged_free_;observed_.capacity_observed_at=staged_at_;}
  staged_capacity_=false;
}
void WorkerReporter::tick(WebDAVClient& remote,bool control_ready){
  const auto now=millis();if(!control_ready||maintenance_active()||!schedule_.due(now)||storage_read_view_active())return;
  const auto b=backup_status();const auto check=connection_check();
  if(b.active||b.scanning||check.state==pro::CheckState::Checking)return;
  const uint64_t observed=utc();
  if(!observed||WiFi.status()!=WL_CONNECTED)return; // Cannot publish a new fact while offline.
  observed_.reported_at=observed;observed_.utc_valid=true;observed_.uptime_ms=now;
  observed_.wifi=Wifi::Connected;observed_.wifi_observed_at=observed;
  observed_.cloud_code=check.code;
  observed_.cloud_checked_at=check.checkedAt>=1767225600ULL&&check.checkedAt<=253402300799ULL?check.checkedAt:0;
  switch(check.state){case pro::CheckState::WaitingWifi:observed_.cloud_check=CloudCheck::WaitingWifi;break;
    case pro::CheckState::WaitingClock:observed_.cloud_check=CloudCheck::WaitingClock;break;
    case pro::CheckState::Checking:observed_.cloud_check=CloudCheck::Checking;break;
    case pro::CheckState::Ready:observed_.cloud_check=CloudCheck::ReadAccessOk;break;
    case pro::CheckState::Verified:observed_.cloud_check=CloudCheck::UploadReadbackOk;break;
    case pro::CheckState::Failed:observed_.cloud_check=CloudCheck::Failed;break;
    default:observed_.cloud_check=CloudCheck::Unknown;}
  switch(storage_owner()){case pro::Owner::Host:observed_.usb=UsbOwnership::Host;break;
    case pro::Owner::Ejected:observed_.usb=UsbOwnership::Ejected;break;
    case pro::Owner::Local:observed_.usb=UsbOwnership::Local;break;
    case pro::Owner::Fault:observed_.usb=UsbOwnership::Fault;break;}
  observed_.sd=storage_media_detected()?Sd::Initialized:Sd::NotInitialized;
  observed_.read_view_active=false; // Nonblocking read-only admission proved this.
  observed_.last_file_verified_at=b.lastBackup>=1767225600ULL&&b.lastBackup<=253402300799ULL?b.lastBackup:0;
  if(b.error!=last_backup_error_){last_backup_error_=b.error;backup_error_at_=b.error?observed:0;}
  observed_.error_subsystem=b.error?Subsystem::Backup:health_error_?Subsystem::Health:Subsystem::None;
  observed_.error_code=b.error?b.error:health_error_;
  observed_.error_observed_at=b.error?backup_error_at_:health_error_?health_error_at_:0;
  const auto result=remote.publishHealth(observed_,{true,true,true});
  if(result.ok){schedule_.acknowledged(millis());health_error_=0;health_error_at_=0;}
  else if(maintenance_active()||result.code==pro::SnapshotChanged)schedule_.cancelled(millis());
  else {schedule_.failed(millis());health_error_=result.code;health_error_at_=utc();}
  // No backup_message/status/receipt write: metadata failure never changes a
  // file's verification, retry record, cloud icon or the owner's pause state.
}
}
#endif
