#pragma once
#ifndef CSP_ENABLE_REMOTE_HEALTH
#define CSP_ENABLE_REMOTE_HEALTH 0
#endif
#if CSP_ENABLE_REMOTE_HEALTH != 0 && CSP_ENABLE_REMOTE_HEALTH != 1
#error CSP_ENABLE_REMOTE_HEALTH must be zero or one
#endif
#if CSP_ENABLE_REMOTE_HEALTH
#include <cstddef>
#include <cstdint>

namespace pro::health {
constexpr size_t ReportCapacity=1024; // Includes the terminating NUL.
enum class Wifi:uint8_t {Unknown,Disconnected,Connecting,Connected};
enum class CloudCheck:uint8_t {Unknown,WaitingWifi,WaitingClock,Checking,ReadAccessOk,UploadReadbackOk,Failed};
enum class UsbOwnership:uint8_t {Unknown,Host,Ejected,Local,Fault};
enum class Sd:uint8_t {Unknown,NotInitialized,Initialized};
enum class Scan:uint8_t {Unknown,Partial,Complete};
enum class Subsystem:uint8_t {None,Wifi,Clock,Cloud,Usb,Sd,Queue,Backup,Setup,Health};

struct Snapshot {
  char firmware[40]{}; // NUL-terminated [A-Za-z0-9._-], no other free text.
  uint64_t boot_id=0,sequence=0;
  bool utc_valid=false;
  uint64_t reported_at=0;
  uint32_t uptime_ms=0;
  Wifi wifi=Wifi::Unknown;
  uint64_t wifi_observed_at=0;
  CloudCheck cloud_check=CloudCheck::Unknown;
  int32_t cloud_code=0;
  uint64_t cloud_checked_at=0;
  UsbOwnership usb=UsbOwnership::Unknown;
  Sd sd=Sd::Unknown;
  bool read_view_active=false;
  bool capacity_known=false;
  uint64_t capacity_total=0,capacity_free=0,capacity_observed_at=0;
  Scan scan=Scan::Unknown;
  uint64_t scan_observed_at=0;
  uint32_t pending=0,failed=0,verified=0,skipped=0;
  uint64_t last_file_verified_at=0;
  Subsystem error_subsystem=Subsystem::None;
  int32_t error_code=0;
  uint64_t error_observed_at=0;
};

// No allocation/storage/network access. Returns only a complete report; on any
// invalid input or insufficient capacity, length=0 and destination starts NUL.
// No byte beyond the caller's capacity is written. Maximum report incl NUL1KiB.
bool encode(const Snapshot&,char* destination,size_t capacity,size_t& length);

class Schedule {
 public:
  static constexpr uint32_t SuccessMs=15*60*1000;
  static constexpr uint32_t FirstRetryMs=5*60*1000;
  static constexpr uint32_t MaxRetryMs=60*60*1000;
  bool due(uint32_t now)const;
  void acknowledged(uint32_t now);
  void failed(uint32_t now);
  void cancelled(uint32_t now);
  uint32_t delay_ms()const{return delay_;}
  uint8_t failures()const{return failures_;}
  bool has_acknowledgment()const{return acknowledged_;}
 private:
  uint32_t at_=0,delay_=0;
  uint8_t failures_=0;
  bool scheduled_=false,acknowledged_=false;
};
} // namespace pro::health
#endif
