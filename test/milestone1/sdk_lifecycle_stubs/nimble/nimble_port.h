#pragma once
#include "esp_err.h"
#define NIMBLE_HS_STACK_SIZE 5120
#define NIMBLE_CORE 0
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nimble_port_deinit(void);
#ifdef __cplusplus
}
#endif
