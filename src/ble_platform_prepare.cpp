#include "ble_platform_prepare.h"
#if CSP_ENABLE_BLE_MANAGEMENT
#include <esp_event.h>
#include <esp_log.h>
#include <esp_bt.h>
#include <mbedtls/bignum.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha512.h>
#include <initializer_list>

namespace {
bool attempted=false,ready=false;
int32_t error=-2500;
}
bool ble_platform_ready(){return ready;}
int32_t ble_platform_error(){return error;}
bool ble_platform_prepare(){
  if(attempted)return ready;
  attempted=true;
  // These are application-lifetime resources, not session allocations. Creating
  // them before USB and the first baseline avoids lazy crypto interrupt/GDMA/
  // lock allocation in the middle of the temporary NimBLE heap. Never rebase a
  // failed session, free SDK globals, skip a self-test or enable RF here.
  for(const char* tag:{"security2","srp6a","esp_srp","protocomm","protocomm_nimble"})
    esp_log_level_set(tag,ESP_LOG_WARN);
  const esp_err_t event=esp_event_loop_create_default();
  if(event!=ESP_OK&&event!=ESP_ERR_INVALID_STATE){error=-2501;return false;}
  // Official public known-answer self-tests, verbose=0; public vectors only.
  // Every temporary context is freed by the SDK tests. Process-global crypto
  // resources intentionally remain initialized and are included in all future
  // heap floors and relative-recovery measurements.
  if(mbedtls_mpi_self_test(0)!=0){error=-2502;return false;}
  if(mbedtls_gcm_self_test(0)!=0){error=-2503;return false;}
  if(mbedtls_sha512_self_test(0)!=0){error=-2504;return false;}
  // Trace2 located the sole retained block splitting the baseline free span:
  // get_desc_for_int's 16-byte SDK registry node. Interrupt release deliberately
  // retains that node. Initialize/deinitialize the controller before USB so its
  // known process-lifetime interrupt metadata is part of the startup budget.
  // Never enable the controller, PHY, NimBLE host, advertising or a connection.
  if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE){error=-2505;return false;}
  esp_bt_controller_config_t config=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  const esp_err_t initialized=esp_bt_controller_init(&config);
  if(initialized!=ESP_OK||esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_INITED){
    error=-2506;return false; // unknown partial ownership stays blocked; no retry
  }
  const esp_err_t stopped=esp_bt_controller_deinit();
  if(stopped!=ESP_OK||esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE){
    error=-2507;return false;
  }
  ready=true;error=0;return true;
}
#endif
