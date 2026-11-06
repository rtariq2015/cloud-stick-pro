#pragma once
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=259;
extern "C" esp_err_t esp_event_loop_create_default();
