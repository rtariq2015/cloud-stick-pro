#pragma once
#include <Arduino.h>
#include "ble_feature.h"
#include "owner_cloud_feature.h"
struct CloudConfig {
  bool is_valid=false;
  bool wifi_open=false;
  String ssid,wifi_password,server_url,username,password,ca_pem,ntp_server="pool.ntp.org";
  uint32_t sync_interval_s=60,max_file_size_mb=64;
  String watch_folder="QA";
  String backup_mode="idle",timezone="EST5EDT,M3.2.0,M11.1.0";
  uint32_t idle_seconds=120,window_start=1,window_end=5;
};
bool validate_config(const CloudConfig&,String& error);
#if CSP_ENABLE_OWNER_CLOUD_SETUP
// The v2 manager owns station credentials separately. This performs the same
// cloud/schedule/CA checks without requiring stale legacy Wi-Fi fields.
bool validate_backup_config(const CloudConfig&,String& error);
// Parse exactly the bounded worker field set without an NVS operation. Prove
// a serialized owner candidate is readable by backups before attempting commit.
bool decode_backup_config_blob(const uint8_t*,size_t,CloudConfig&);
#endif
CloudConfig load_config();
#if CSP_ENABLE_BACKUP_SETUP
// Read-only cloud/schedule load; station credentials belong to the v2 manager.
CloudConfig load_backup_config();
#endif
bool save_config(const CloudConfig&,String& error);
bool reset_config();
