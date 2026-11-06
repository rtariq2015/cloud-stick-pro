#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <esp_bt.h>

struct lifecycle_faults {
  int init_result, gatt_result, name_result, task_result, sync_result;
  int stop_result, deinit_result;
  bool deinit_stays_enabled;
  bool init_stays_enabled;
  bool delay_before_loop;
  bool observed_loop_allowed;
  unsigned init_calls, gatt_free_calls, cleanup_calls, deinit_calls;
  unsigned host_body_calls, task_deletes, scheduled_at, tick;
  esp_bt_controller_status_t controller;
};
#ifdef __cplusplus
extern "C" {
#endif
extern struct lifecycle_faults faults;
void lifecycle_test_host_yield(void);
void lifecycle_test_late_sync(void);
#ifdef __cplusplus
}
#endif
