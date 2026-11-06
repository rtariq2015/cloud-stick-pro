#pragma once
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_NOT_SUPPORTED 0x106
typedef struct esp_flash_t esp_flash_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_flash_write(esp_flash_t*,const void*,uint32_t,uint32_t);
esp_err_t esp_flash_write_encrypted(esp_flash_t*,uint32_t,const void*,uint32_t);
esp_err_t esp_flash_erase_region(esp_flash_t*,uint32_t,uint32_t);
esp_err_t esp_flash_erase_chip(esp_flash_t*);
#ifdef __cplusplus
}
#endif
