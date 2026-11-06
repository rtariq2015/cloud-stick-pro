#pragma once
#include "common.h"
struct esp_flash_t {int unused;};
extern "C" {
esp_err_t esp_flash_write(esp_flash_t*,const void*,uint32_t,uint32_t);
esp_err_t esp_flash_write_encrypted(esp_flash_t*,uint32_t,const void*,uint32_t);
esp_err_t esp_flash_erase_region(esp_flash_t*,uint32_t,uint32_t);
esp_err_t esp_flash_erase_chip(esp_flash_t*);
}
