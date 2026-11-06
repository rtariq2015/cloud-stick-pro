#pragma once
#include <stdbool.h>
#include "protocomm.h"
#include "esp_err.h"
typedef struct { bool keep_ble_on; } protocomm_ble_config_t;
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t protocomm_ble_start(protocomm_t*,const protocomm_ble_config_t*);
esp_err_t protocomm_ble_stop(protocomm_t*);
#ifdef __cplusplus
}
#endif
