#pragma once
#include "ble_feature.h"
#ifndef CSP_ENABLE_OWNER_CLOUD_SETUP
#define CSP_ENABLE_OWNER_CLOUD_SETUP 0
#endif
#if CSP_ENABLE_OWNER_CLOUD_SETUP != 0 && CSP_ENABLE_OWNER_CLOUD_SETUP != 1
#error CSP_ENABLE_OWNER_CLOUD_SETUP must be 0 or 1
#endif
#if CSP_ENABLE_OWNER_CLOUD_SETUP && (!CSP_ENABLE_BACKUP_SETUP || !CSP_ENABLE_WIFI_SETUP || !CSP_ENABLE_BLE_MANAGEMENT || CSP_BLE_ISOLATED_TEST)
#error Owner cloud setup requires the separate managed private-owner Wi-Fi build
#endif
