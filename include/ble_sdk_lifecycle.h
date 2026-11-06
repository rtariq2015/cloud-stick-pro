#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>
#include <protocomm.h>
#include <protocomm_ble.h>

#ifdef __cplusplus
extern "C" {
#endif

// Owner-task only. Start requires a bounded successful host sync/advertising
// acknowledgement. A failure never authorizes deleting pc without idle proof.
esp_err_t csp_ble_sdk_start(protocomm_t *pc, const protocomm_ble_config_t *config);
esp_err_t csp_ble_sdk_stop(protocomm_t *pc);
bool csp_ble_sdk_idle(void);
bool csp_ble_sdk_ready(void);
// Sampled by the host itself immediately before its exit acknowledgement;
// zero means no completed sample. Pinned IDF FreeRTOS returns bytes.
uint32_t csp_ble_sdk_host_stack_min_bytes(void);

#ifdef __cplusplus
}
#endif
