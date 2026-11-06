#include "ble_feature.h"
#if CSP_ENABLE_BACKUP_SETUP
#include <esp_wifi.h>
#include <algorithm>

// This project-local wrapper changes documented esp_wifi_init arguments, not
// precompiled-library configuration or ABI. Only combined builds link --wrap.
// Retain the pinned qio_qspi AMPDU RX window16; smaller compatible counts stay
// smaller. These limits bound burst capacity, not a guaranteed heap saving.
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t* config);
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t* config){
  constexpr int DynamicTxLimit=8,DynamicRxLimit=16;
  if(!config||config->magic!=WIFI_INIT_CONFIG_MAGIC||config->tx_buf_type!=1||
     config->static_tx_buf_num!=0||config->static_rx_buf_num<2||config->static_rx_buf_num>25||
     config->dynamic_tx_buf_num<1||config->dynamic_tx_buf_num>128||
     config->dynamic_rx_buf_num<1||config->dynamic_rx_buf_num>128||
     (config->ampdu_rx_enable!=0&&config->ampdu_rx_enable!=1))return ESP_ERR_INVALID_ARG;
  auto adjusted=*config;
  adjusted.dynamic_tx_buf_num=std::min(config->dynamic_tx_buf_num,DynamicTxLimit);
  adjusted.dynamic_rx_buf_num=std::min(config->dynamic_rx_buf_num,DynamicRxLimit);
  if(adjusted.dynamic_rx_buf_num<adjusted.static_rx_buf_num||
     (adjusted.ampdu_rx_enable&&(adjusted.rx_ba_win<2||adjusted.rx_ba_win>32||
                               adjusted.dynamic_rx_buf_num<adjusted.rx_ba_win)))return ESP_ERR_INVALID_ARG;
  return __real_esp_wifi_init(&adjusted);
}
#endif
