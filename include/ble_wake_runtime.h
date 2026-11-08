// SPDX-License-Identifier: MIT
#pragma once
#include "ble_feature.h"
#include "owner_login_feature.h"
#include "local_dashboard.h"
#include "ble_wake.h"
#if CSP_ENABLE_BLE_WIFI_WAKE
struct WifiSetupSession;
namespace pro::ble_wake {
void initialize(const std::array<uint8_t,6>&,uint64_t);
void revoke(uint64_t);
bool pairPhone(const WifiSetupSession&,const PublicKey&,Id&);
bool paired();
Result challenge(uint32_t,bool,Nonce&);
Result authorize(uint32_t,bool,const Nonce&,const uint8_t*,size_t);
Id identity();
}
#endif
