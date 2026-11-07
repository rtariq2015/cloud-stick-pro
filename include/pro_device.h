#pragma once
#include "config_loader.h"
#include "pro_core.h"
#include "pro_diagnostics.h"
#include "pro_transfer.h"
#include <Arduino.h>
#include "ble_feature.h"
#if defined(CSP_PRODUCT_WIFI_DEV_PROFILE) && CSP_PRODUCT_WIFI_DEV_PROFILE
constexpr const char* FirmwareVersion="0.4.4-wifi-dev";
#elif defined(CSP_PRODUCT_UI_DEV_PROFILE) && CSP_PRODUCT_UI_DEV_PROFILE
constexpr const char* FirmwareVersion="0.4.3-ui-dev";
#elif defined(CSP_PRODUCT_INCREMENTAL_DEV_PROFILE) && CSP_PRODUCT_INCREMENTAL_DEV_PROFILE
constexpr const char* FirmwareVersion="0.4.2-incremental-dev";
#elif defined(CSP_PRODUCT_SETUP_DEV_PROFILE) && CSP_PRODUCT_SETUP_DEV_PROFILE
constexpr const char* FirmwareVersion="0.4.1-setup-dev";
#elif defined(CSP_PRODUCT_RC5_PROFILE) && CSP_PRODUCT_RC5_PROFILE
constexpr const char* FirmwareVersion="0.4.0-rc5";
#elif defined(CSP_PRODUCT_RC4_PROFILE) && CSP_PRODUCT_RC4_PROFILE
constexpr const char* FirmwareVersion="0.4.0-rc4";
#elif defined(CSP_PRODUCT_RC3_PROFILE) && CSP_PRODUCT_RC3_PROFILE
constexpr const char* FirmwareVersion="0.4.0-rc3";
#elif defined(CSP_PRODUCT_RC2_PROFILE) && CSP_PRODUCT_RC2_PROFILE
constexpr const char* FirmwareVersion="0.4.0-rc2";
#elif defined(CSP_PRODUCT_SPEED2_PROFILE) && CSP_PRODUCT_SPEED2_PROFILE
constexpr const char* FirmwareVersion="0.4.0-speed2";
#elif defined(CSP_PRODUCT_SPEED_TRIAL) && CSP_PRODUCT_SPEED_TRIAL
constexpr const char* FirmwareVersion="0.4.0-speed1";
#elif defined(CSP_PRODUCT_DASHBOARD_TRIAL) && CSP_PRODUCT_DASHBOARD_TRIAL
constexpr const char* FirmwareVersion="0.4.0-dashboard1";
#elif defined(CSP_PRODUCT_RC_PROFILE) && CSP_PRODUCT_RC_PROFILE
constexpr const char* FirmwareVersion="0.4.0-rc1";
#elif defined(CSP_PRODUCT_LAB7_PROFILE) && CSP_PRODUCT_LAB7_PROFILE
constexpr const char* FirmwareVersion="0.4.0-product-lab7";
#elif defined(CSP_PRODUCT_LAB6_PROFILE) && CSP_PRODUCT_LAB6_PROFILE
constexpr const char* FirmwareVersion="0.4.0-product-lab6";
#elif defined(CSP_PRODUCT_LAB5_PROFILE) && CSP_PRODUCT_LAB5_PROFILE
constexpr const char* FirmwareVersion="0.4.0-product-lab5";
#elif defined(CSP_PRODUCT_LAB4_PROFILE) && CSP_PRODUCT_LAB4_PROFILE
constexpr const char* FirmwareVersion="0.4.0-product-lab4";
#elif defined(CSP_PRODUCT_LAB3_PROFILE) && CSP_PRODUCT_LAB3_PROFILE
constexpr const char* FirmwareVersion="0.4.0-product-lab3";
#elif defined(CSP_PRODUCT_LAB2_PROFILE) && CSP_PRODUCT_LAB2_PROFILE
constexpr const char* FirmwareVersion="0.4.0-product-lab2";
#elif defined(CSP_PRODUCT_LAB_PROFILE) && CSP_PRODUCT_LAB_PROFILE
constexpr const char* FirmwareVersion="0.4.0-product-lab1";
#elif defined(CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE) && CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE && defined(CSP_PUBLIC_WIFI_MEMORY_PROFILE) && CSP_PUBLIC_WIFI_MEMORY_PROFILE
constexpr const char* FirmwareVersion="0.3.3-public-wifi-mem1";
#elif defined(CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE) && CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE
constexpr const char* FirmwareVersion="0.3.3-public-wifi-lab1";
#elif CSP_BACKUP_FIXTURE_ONLY
constexpr const char* FirmwareVersion="0.3.3-backup-wifi-lab3";
#elif CSP_ENABLE_BACKUP_SETUP
constexpr const char* FirmwareVersion="0.3.3-backup-wifi-dev3";
#elif CSP_BACKUP_LINK_PROBE
constexpr const char* FirmwareVersion="0.3.3-backup-link-probe";
#elif CSP_USB_BENCH_AUTOMATION && CSP_BLE_ALLOC_TRACE
constexpr const char* FirmwareVersion="0.3.3-wifi-m2-bench-trace4";
#elif CSP_BENCH_RADIO_RELEASE
constexpr const char* FirmwareVersion="0.3.3-wifi-m2-radio3";
#elif CSP_USB_BENCH_AUTOMATION
constexpr const char* FirmwareVersion="0.3.3-wifi-m2-bench1";
#elif CSP_ENABLE_WIFI_SETUP
constexpr const char* FirmwareVersion="0.3.3-wifi-m2-resetdiag1";
#elif CSP_BLE_ALLOC_TRACE
constexpr const char* FirmwareVersion="0.3.3-ble-m1c-trace3";
#elif CSP_ENABLE_BLE_MANAGEMENT
constexpr const char* FirmwareVersion="0.3.3-ble-m1c-prep2";
#else
constexpr const char* FirmwareVersion="0.3.3-dev";
#endif
struct BackupStatus {
  uint32_t pending=0,verified=0,failed=0,transferred=0,skipped=0;
  uint64_t total=0,free=0,lastBackup=0;
  uint32_t minimumHeap=0,largestBlock=0,stackFree=0;
  bool scanning=false,active=false,cloud=false,capacityKnown=false,hasScan=false,scanComplete=false;
  int progress=-1,error=0;
  pro::TransferPhase phase=pro::TransferPhase::None;
  uint64_t transferDone=0,transferTotal=0;uint32_t transferSpeed=0;
  char message[96]="Waiting for automatic backup";
  char currentFile[pro::PathLimit+1]{};
};
BackupStatus backup_status();
#if defined(CSP_ENABLE_OWNER_CLOUD_SETUP) && CSP_ENABLE_OWNER_CLOUD_SETUP
// Owner-only RAM invalidation after a durable destination change. Requires the
// managed worker fully Stopped; journal/SD files are never changed here.
bool backup_invalidate_cloud_status();
#endif
void backup_message(const char*,int error=0);
void backup_progress(uint64_t done,uint64_t total);
void backup_transfer_begin(pro::TransferPhase,uint64_t total);
void start_backup(const CloudConfig&);
void set_maintenance(bool enabled);
bool maintenance_active();
struct ConnectionCheck {
  pro::CheckState state=pro::CheckState::Idle;
  int code=0;uint64_t checkedAt=0;
  char testFile[72]{};
};
ConnectionCheck connection_check();
bool request_connection_check(bool uploadTest=false);
void allow_connection_checks(bool enabled);
struct BackupPolicy {bool background=true,overnight=false;uint32_t idleSeconds=120,startHour=1,endHour=5;};
BackupPolicy backup_policy();
const String& backup_folder();
bool backup_window_open();
std::string hash_text(const std::string&);
bool hash_file(const std::string&,std::string&,uint64_t&);
