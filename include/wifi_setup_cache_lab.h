#pragma once
#include "ble_feature.h"
#include <cstdint>
#if CSP_NVS_CACHE_LAB
struct WifiCacheLabResult {
  bool accepted=false;
  int32_t sdk_error=0;
  uint32_t writes=0,before_largest=0,after_largest=0;
};
// Only public synthetic uint32 values in the fixed service namespace. No input
// namespace/key/value, owner record, Wi-Fi profile, cloud config or file access.
WifiCacheLabResult wifi_setup_cache_lab();
int32_t wifi_setup_cache_lab_clear();
#endif
