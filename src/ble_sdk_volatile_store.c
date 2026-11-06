#include "ble_feature.h"
#if CSP_ENABLE_BLE_MANAGEMENT && (CSP_BLE_ISOLATED_TEST || CSP_ENABLE_WIFI_SETUP)
/*
 * Isolated M1 policy backend for the pinned NimBLE RAM store. The SDK RAM store
 * still owns/clears its records normally; this adapter never loads prior bonds
 * or persists keys/CCCDs/IRKs. No-bonding alone does not disable the pinned
 * CONFIG_BT_NIMBLE_NVS_PERSIST backend. These exact ABI symbols resolve before
 * libbt.a, so its complete ble_store_nvs.c.obj must be absent from the link map.
 * See sdk/pinned_ble/README.md and PROVENANCE.json.
 */
void ble_store_config_conf_init(void) {}
int ble_store_config_persist_cccds(void) { return 0; }
int ble_store_config_persist_csfcs(void) { return 0; }
int ble_store_config_persist_eads(void) { return 0; }
int ble_store_config_persist_local_irk(void) { return 0; }
int ble_store_config_persist_rpa_recs(void) { return 0; }
int ble_store_config_persist_peer_secs(void) { return 0; }
int ble_store_config_persist_our_secs(void) { return 0; }
int ble_store_persist_peer_records(void) { return 0; }
#endif
