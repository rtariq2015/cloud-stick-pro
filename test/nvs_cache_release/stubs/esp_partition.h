#pragma once
#include <esp_err.h>
#include <cstddef>
#include <cstdint>
constexpr int ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_NVS=2;
struct esp_partition_t {uint32_t address,size;};
const esp_partition_t* esp_partition_find_first(int,int,const char*);
esp_err_t esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
