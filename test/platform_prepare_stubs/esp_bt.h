#pragma once
enum esp_bt_controller_status_t {ESP_BT_CONTROLLER_STATUS_IDLE,ESP_BT_CONTROLLER_STATUS_INITED,ESP_BT_CONTROLLER_STATUS_ENABLED};
struct esp_bt_controller_config_t {int synthetic_defaults;};
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() {1234}
extern "C" esp_bt_controller_status_t esp_bt_controller_get_status();
extern "C" int esp_bt_controller_init(esp_bt_controller_config_t*);
extern "C" int esp_bt_controller_deinit();
