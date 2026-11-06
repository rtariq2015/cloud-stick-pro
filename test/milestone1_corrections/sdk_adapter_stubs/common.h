#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <sys/types.h>
using esp_err_t=int;
using protocomm_security_handle_t=void*;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_ARG=0x102,
  ESP_ERR_INVALID_STATE=0x103, ESP_ERR_INVALID_SIZE=0x104,
  ESP_ERR_NO_MEM=0x105, ESP_ERR_TIMEOUT=0x106;
constexpr int ESP_LOG_WARN=2, ESP_MAC_BT=1, MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
struct protocomm_t { int unused; };
struct protocomm_security_t {
 int ver; uint8_t patch_ver;
 esp_err_t (*init)(void**);esp_err_t (*cleanup)(void*);
 esp_err_t (*new_transport_session)(void*,uint32_t);
 esp_err_t (*close_transport_session)(void*,uint32_t);
 esp_err_t (*security_req_handler)(void*,const void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*,void*);
 esp_err_t (*encrypt)(void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*);
 esp_err_t (*decrypt)(void*,uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*);
};
struct protocomm_security2_params_t { const char* salt;uint16_t salt_len;const char* verifier;uint16_t verifier_len; };
struct protocomm_ble_name_uuid_t { const char* name;uint16_t uuid; };
struct protocomm_ble_config_t {
 char device_name[32];uint8_t service_uuid[16];
 protocomm_ble_name_uuid_t* nu_lookup;size_t nu_lookup_count;
 bool ble_bonding,ble_sm_sc,ble_link_encryption,keep_ble_on,ble_notify;
};
extern "C" { extern const protocomm_security_t protocomm_security2; }
uint32_t millis();
extern int simulatedStopCalls;
extern bool simulatedStopOk;
inline void esp_log_level_set(const char*,int){}
inline int esp_event_loop_create_default(){return ESP_OK;}
inline int esp_read_mac(uint8_t*,int){return ESP_OK;}
inline size_t heap_caps_get_free_size(int){return 500000;}
inline size_t heap_caps_get_largest_free_block(int){return 250000;}
inline protocomm_t* protocomm_new(){return new protocomm_t{};}
inline void protocomm_delete(protocomm_t* pc){delete pc;}
inline int protocomm_set_security(protocomm_t*,const char*,const protocomm_security_t*,const void*){return ESP_OK;}
using handler_t=esp_err_t(*)(uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*,void*);
inline int protocomm_add_endpoint(protocomm_t*,const char*,handler_t,void*){return ESP_OK;}
inline int protocomm_set_version(protocomm_t*,const char*,const char*){return ESP_OK;}
inline int protocomm_ble_start(protocomm_t*,const protocomm_ble_config_t*){return ESP_OK;}
// Return failure so the harness never pretends the blocked callback has drained.
inline int protocomm_ble_stop(protocomm_t*){++simulatedStopCalls;return simulatedStopOk?ESP_OK:ESP_FAIL;}
