#pragma once
#include <cstdint>
#include "ble_feature.h"

struct BleManagementStatus {
  bool active=false,connected=false,authenticated=false,terminal_denial=false;
  bool stopping=false,teardown_failed=false;
  uint32_t window_generation=0,window_started_ms=0,connected_ms=0,last_activity_ms=0;
  int32_t last_error=0;
};

// Milestone 1 is synthetic/read-only. Root lifecycle code must quiesce backup
// and disable legacy setup before calling begin. No API here changes settings.
#if CSP_ENABLE_BLE_MANAGEMENT
bool ble_management_begin();
void ble_management_poll();
bool ble_management_stop();
bool ble_management_active();
bool ble_management_authenticated();
int32_t ble_management_last_error();
BleManagementStatus ble_management_status();
void ble_management_set_connection_status(bool wifi_connected);
#else
inline bool ble_management_begin() { return false; }
inline void ble_management_poll() {}
inline bool ble_management_stop() { return true; }
inline bool ble_management_active() { return false; }
inline bool ble_management_authenticated() { return false; }
inline int32_t ble_management_last_error() { return 0; }
inline BleManagementStatus ble_management_status() { return {}; }
inline void ble_management_set_connection_status(bool) {}
#endif
