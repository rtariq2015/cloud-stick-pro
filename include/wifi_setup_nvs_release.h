#pragma once
#include "ble_feature.h"
#include <cstdint>
#if CSP_ENABLE_BLE_NVS_CACHE_RELEASE
enum class WifiNvsReleaseStep:uint8_t {
  Never,Partition,Allocate,ReadBefore,Deinit,Init,ReadAfter,Changed,Verified
};
struct WifiNvsReleaseStatus {
  WifiNvsReleaseStep step=WifiNvsReleaseStep::Never;
  int32_t sdk_error=0;
  uint32_t before_largest=0,after_largest=0;
  bool flash_equal=false;
};
// Owner loop only, after verified BLE/controller stop, Wi-Fi driver release and
// worker reclamation. No retained NVS handle may cross this boundary.
bool wifi_setup_nvs_release_cache();
WifiNvsReleaseStatus wifi_setup_nvs_release_status();
#endif
