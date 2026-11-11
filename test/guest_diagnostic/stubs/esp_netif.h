#pragma once
#include <cstdint>
constexpr int ESP_OK=0;
struct esp_ip4_addr_t {uint32_t addr;};
struct esp_netif_ip_info_t {esp_ip4_addr_t ip,netmask,gw;};
int esp_netif_get_ip_info(void*,esp_netif_ip_info_t*);
