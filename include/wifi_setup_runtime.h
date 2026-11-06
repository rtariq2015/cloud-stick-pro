#pragma once
#include "ble_feature.h"
#include "owner_login_feature.h"
#if CSP_ENABLE_WIFI_SETUP
#include <cstddef>
#include <cstdint>
#include <esp_err.h>
#include <protocomm_security2.h>
namespace pro::wifi_setup {struct WireRequest;}
struct WifiSetupSession {uint32_t id=0;uint64_t generation=0;};
void wifi_setup_runtime_setup();
void wifi_setup_runtime_loop();
bool wifi_setup_owner_parameters(protocomm_security2_params_t&);
esp_err_t wifi_setup_endpoint(const WifiSetupSession&,const pro::wifi_setup::WireRequest&,const uint8_t*,size_t,
                             uint8_t**,ssize_t*);
// Coherent admission proof provided by the existing Security 2 adapter. No
// owner/permission/success field is ever accepted from the phone.
bool ble_management_authorizes(const WifiSetupSession&);
#if CSP_ENABLE_OWNER_LOGIN
#include <mutex>
// The short publication lease serializes the commit barrier with one bounded
// SDK encryption. It never wraps NVS/network/stop and is not stateMutex.
std::mutex& wifi_setup_owner_login_output_mutex();
bool wifi_setup_owner_login_info_allowed();
bool wifi_setup_owner_login_output_allowed(const WifiSetupSession&,const uint8_t*,size_t,uint32_t& ack_ticket);
void wifi_setup_owner_login_ack_encrypted(const WifiSetupSession&,uint32_t ack_ticket);
#endif
#endif
