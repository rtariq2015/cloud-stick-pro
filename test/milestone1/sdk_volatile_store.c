// Real pinned RAM-store initialization with fake callback bodies. It must reset
// all RAM counters and register callbacks while our backend never opens NVS.
#include <assert.h>
#include <stdio.h>
#define MYNEWT_VAL(x) MYNEWT_VAL_##x
#define MYNEWT_VAL_BLE_STATIC_TO_DYNAMIC 0
#define MYNEWT_VAL_ENC_ADV_DATA 0
#define SYSINIT_ASSERT_ACTIVE() ((void)0)
static int ble_store_config_read(void) { return 1; }
static int ble_store_config_write(void) { return 2; }
static int ble_store_config_delete(void) { return 3; }
static struct { int (*store_read_cb)(void),(*store_write_cb)(void),(*store_delete_cb)(void); } ble_hs_cfg;
static int ble_store_config_num_our_secs=3,ble_store_config_num_peer_secs=3,ble_store_config_num_cccds=3;
static int ble_store_config_num_csfcs=3,ble_store_config_num_rpa_recs=3,ble_store_config_num_local_irks=3;
void ble_store_config_conf_init(void);
int ble_store_config_persist_cccds(void);
int ble_store_config_persist_csfcs(void);
int ble_store_config_persist_eads(void);
int ble_store_config_persist_local_irk(void);
int ble_store_config_persist_rpa_recs(void);
int ble_store_config_persist_peer_secs(void);
int ble_store_config_persist_our_secs(void);
int ble_store_persist_peer_records(void);
#include "ble_store_config_init.exact.inc"
int main(void) {
    ble_store_config_init();
    assert(ble_hs_cfg.store_read_cb==ble_store_config_read);
    assert(ble_hs_cfg.store_write_cb==ble_store_config_write);
    assert(ble_hs_cfg.store_delete_cb==ble_store_config_delete);
    assert(!ble_store_config_num_our_secs && !ble_store_config_num_peer_secs && !ble_store_config_num_cccds);
    assert(!ble_store_config_num_csfcs && !ble_store_config_num_rpa_recs && !ble_store_config_num_local_irks);
    assert(!ble_store_config_persist_cccds() && !ble_store_config_persist_csfcs());
    assert(!ble_store_config_persist_eads() && !ble_store_config_persist_local_irk());
    assert(!ble_store_config_persist_rpa_recs() && !ble_store_config_persist_peer_secs());
    assert(!ble_store_config_persist_our_secs() && !ble_store_persist_peer_records());
    puts("volatile store: SDK RAM callbacks registered/counters reset; no NVS backend dependencies");
}
