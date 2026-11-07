#pragma once
#include <esp_err.h>
#include <cstddef>
#include <cstdint>
using nvs_handle_t=unsigned;
constexpr int NVS_READWRITE=1;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND=0x1102,ESP_ERR_INVALID_STATE=0x103;
struct nvs_stats_t {size_t used_entries=0,free_entries=0,available_entries=0,total_entries=0,namespace_count=0;};
esp_err_t nvs_get_stats(const char*,nvs_stats_t*);
esp_err_t nvs_open(const char*,int,nvs_handle_t*);
esp_err_t nvs_set_u32(nvs_handle_t,const char*,uint32_t);
esp_err_t nvs_get_u32(nvs_handle_t,const char*,uint32_t*);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_erase_key(nvs_handle_t,const char*);
void nvs_close(nvs_handle_t);
