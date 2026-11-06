#pragma once
#include "remote_health_model.h"
#if CSP_ENABLE_REMOTE_HEALTH
class WebDAVClient;
struct BackupStatus;
namespace pro::health {
// One managed worker owns this object. Observations are made at actual source
// events; tick is called only at proven read-view-closed boundaries.
class WorkerReporter {
 public:
  WorkerReporter();
  void scanStarted();
  void scanProgress(const BackupStatus&);
  void capacityMeasured(bool,uint64_t total,uint64_t free);
  void scanEnded(const BackupStatus&,bool consistent);
  void tick(WebDAVClient&,bool control_ready);
 private:
  Snapshot observed_;
  Schedule schedule_;
  bool staged_capacity_=false;
  uint64_t staged_total_=0,staged_free_=0,staged_at_=0;
  int32_t last_backup_error_=0,health_error_=0;
  uint64_t backup_error_at_=0,health_error_at_=0;
};
}
#endif
