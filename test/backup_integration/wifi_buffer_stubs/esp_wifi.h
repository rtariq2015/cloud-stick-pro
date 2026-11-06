#pragma once
#include <cstdint>

using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_ARG=0x102,ESP_ERR_NO_MEM=0x101;
#define WIFI_INIT_CONFIG_MAGIC 0x1F2F3F4F

// Synthetic host structure retains every named field in the pinned SDK type,
// including opaque OS/crypto callbacks. It does not establish target ABI;
// the separate firmware build must compile against the actual esp_wifi.h.
struct wpa_crypto_funcs_t {uintptr_t callbacks[24];};
struct wifi_init_config_t {
 void* osi_funcs;
 wpa_crypto_funcs_t wpa_crypto_funcs;
 int static_rx_buf_num,dynamic_rx_buf_num,tx_buf_type,static_tx_buf_num,dynamic_tx_buf_num;
 int rx_mgmt_buf_type,rx_mgmt_buf_num,cache_tx_buf_num,csi_enable;
 int ampdu_rx_enable,ampdu_tx_enable,amsdu_tx_enable,nvs_enable,nano_enable,rx_ba_win;
 int wifi_task_core_id,beacon_max_len,mgmt_sbuf_num;
 uint64_t feature_caps;
 bool sta_disconnected_pm;
 int espnow_max_encrypt_num,tx_hetb_queue_num;
 bool dump_hesigb_enable;
 int magic;
};
