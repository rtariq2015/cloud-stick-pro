// C shim around functions extracted verbatim from the checked-in patched SDK.
// Only OS/controller/GATT effects are mocked; the tested lifecycle code is real.
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "ble_sdk_internal.h"
#include "ble_sdk_lifecycle.h"
#include "sdk_lifecycle_faults.h"

#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define MYNEWT_VAL(x) 1
#define BLE_SM_IO_CAP_NO_IO 0
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
#define BLE_HS_ADV_F_DISC_GEN 1
#define BLE_HS_ADV_F_BREDR_UNSUP 2
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 1
#define BLE_HS_FOREVER 0
#define BLE_HS_EDISABLED 8

typedef struct { bool keep_ble_on, ble_bonding, ble_sm_sc; const char *device_name; void *gatt_db; } simple_ble_cfg_t;
typedef struct { protocomm_t *pc_ble; } transport_t;
static transport_t transport;
static transport_t *protoble_internal;
static simple_ble_cfg_t config, *ble_cfg_p;
struct ble_hs_adv_fields { void *uuids128; const char *name; int name_len,name_is_complete,flags,num_uuids128,uuids128_is_complete; void *mfg_data; int mfg_data_len; };
static struct ble_hs_adv_fields adv_data, resp_data;
static struct { int conn_mode,disc_mode,itvl_min,itvl_max; } adv_params;
static struct { void (*reset_cb)(int); void (*sync_cb)(void); void (*gatts_register_cb)(void); void (*store_status_cb)(void); int sm_io_cap,sm_bonding,sm_mitm,sm_sc,sm_our_key_dist,sm_their_key_dist; } ble_hs_cfg;
static uint8_t *protocomm_ble_addr, own_addr_type;
static void *protocomm_ble_mfg_data;
static int protocomm_ble_mfg_data_len;

static int ble_gap_adv_stop(void) { return ESP_OK; }
static void free_gatt_ble_misc_memory(simple_ble_cfg_t *cfg) { (void)cfg; ++faults.gatt_free_calls; }
static void protocomm_ble_cleanup(void) { ++faults.cleanup_calls; protoble_internal=NULL; }
static int nimble_port_init(void) { ++faults.init_calls; faults.controller = faults.init_result && !faults.init_stays_enabled ? ESP_BT_CONTROLLER_STATUS_IDLE : ESP_BT_CONTROLLER_STATUS_ENABLED; return faults.init_result; }
int nimble_port_deinit(void) { ++faults.deinit_calls; if (!faults.deinit_result && !faults.deinit_stays_enabled) faults.controller=ESP_BT_CONTROLLER_STATUS_IDLE; return faults.deinit_result; }
static int nimble_port_stop(void) { if (faults.stop_result) return faults.stop_result; csp_ble_sdk_host_task_exit(); return ESP_OK; }
static void simple_ble_on_reset(int reason) { (void)reason; csp_ble_sdk_note_sync(ESP_FAIL); }
static void gatt_svr_register_cb(void) {}
static void ble_store_util_status_rr(void) {}
static int gatt_svr_init(const simple_ble_cfg_t *cfg) { (void)cfg; return faults.gatt_result; }
static int ble_svc_gap_device_name_set(const char *name) { (void)name; return faults.name_result; }
static const char *ble_svc_gap_device_name(void) { return "synthetic"; }
static void ble_store_config_init(void) {}
static int ble_hs_id_set_rnd(const uint8_t *address) { (void)address; return faults.sync_result; }
static int ble_hs_util_ensure_addr(int random) { (void)random; return faults.sync_result; }
static int ble_hs_id_infer_auto(int random,uint8_t *type) { (void)random; *type=0; return faults.sync_result; }
static int ble_gap_adv_set_fields(const void *fields) { (void)fields; return ESP_OK; }
static int ble_gap_adv_rsp_set_fields(const void *fields) { (void)fields; return ESP_OK; }
static int simple_ble_gap_event(void *event,void *arg) { (void)event; (void)arg; return 0; }
static int ble_gap_adv_start(int type,void *address,int duration,const void *params,int (*cb)(void*,void*),void *arg) { (void)type;(void)address;(void)duration;(void)params;(void)cb;(void)arg; return ESP_OK; }
static void simple_ble_on_sync(void);
static void nimble_host_task(void *arg) {
    (void)arg; ++faults.host_body_calls;
    if (faults.delay_before_loop) {
        faults.observed_loop_allowed=csp_ble_sdk_start_allowed();
        lifecycle_test_host_yield();
    }
    if (!csp_ble_sdk_enter_host_loop()) { csp_ble_sdk_host_task_exit(); return; }
    if (faults.sync_result != -999) simple_ble_on_sync();
    lifecycle_test_host_yield();
}

#include "esp_nimble_enable.patched.inc"
#include "simple_ble_advertise.patched.inc"
#include "simple_ble_on_sync.patched.inc"
#include "simple_ble_start.patched.inc"
#include "protocomm_ble_stop.patched.inc"

esp_err_t protocomm_ble_start(protocomm_t *pc,const protocomm_ble_config_t *given) {
    config.keep_ble_on=given->keep_ble_on;
    config.device_name="synthetic";
    protoble_internal=&transport; transport.pc_ble=pc;
    adv_data.uuids128=&transport;
    esp_err_t err=simple_ble_start(&config);
    if (err != ESP_OK) {
        // This branch is also verified against the exact patched source by the
        // build harness. Retain ownership when failed init/deinit is uncertain.
        if (!csp_ble_sdk_controller_idle() || !csp_ble_sdk_wait_host_exit()) return err;
        free_gatt_ble_misc_memory(&config); protocomm_ble_cleanup();
    }
    return err;
}
bool csp_ble_sdk_transport_present(void) { return protoble_internal != NULL; }
void lifecycle_test_late_sync(void) { simple_ble_on_sync(); }
