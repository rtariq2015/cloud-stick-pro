#pragma once
#include "ble_feature.h"
#include <stdint.h>
#if CSP_ENABLE_BLE_MANAGEMENT
// Owner task, once during boot before USB/media initialization. Never starts RF.
bool ble_platform_prepare();
bool ble_platform_ready();
int32_t ble_platform_error();
#endif
