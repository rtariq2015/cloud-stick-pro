#pragma once
#include "ble_feature.h"
#ifndef CSP_ENABLE_OWNER_LOGIN
#define CSP_ENABLE_OWNER_LOGIN 0
#endif
#if CSP_ENABLE_OWNER_LOGIN != 0 && CSP_ENABLE_OWNER_LOGIN != 1
#error CSP_ENABLE_OWNER_LOGIN must be 0 or 1
#endif
#if CSP_ENABLE_OWNER_LOGIN && (!CSP_ENABLE_BACKUP_SETUP || !CSP_ENABLE_WIFI_SETUP || !CSP_ENABLE_BLE_MANAGEMENT || CSP_BLE_ISOLATED_TEST)
#error Owner login requires the managed private-owner Wi-Fi build
#endif
