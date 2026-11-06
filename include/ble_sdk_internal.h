#pragma once

#include <stdbool.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hooks used exclusively by the reviewed, pinned SDK corrections.
esp_err_t csp_ble_sdk_create_host_task(TaskFunction_t host_task);
esp_err_t csp_ble_sdk_host_task_exit(void);
void csp_ble_sdk_note_sync(esp_err_t result);
bool csp_ble_sdk_start_allowed(void);
bool csp_ble_sdk_enter_host_loop(void);
bool csp_ble_sdk_commit_host_loop(void);
bool csp_ble_sdk_host_running(void);
bool csp_ble_sdk_wait_host_exit(void);
bool csp_ble_sdk_controller_idle(void);
esp_err_t csp_ble_sdk_deinit_port(void);
void csp_ble_sdk_note_init_failure(esp_err_t result);
bool csp_ble_sdk_transport_present(void);

#ifdef __cplusplus
}
#endif
