// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
struct WifiSetupSession {uint32_t id=0;uint64_t generation=0;};
bool ble_management_authorizes(const WifiSetupSession&);
bool wifi_setup_owner_login_info_allowed();
