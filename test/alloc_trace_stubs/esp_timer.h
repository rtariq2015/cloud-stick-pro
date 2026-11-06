#pragma once
typedef int esp_err_t;
constexpr esp_err_t ESP_OK=0;
typedef void* esp_timer_handle_t;
struct esp_timer_create_args_t {void (*callback)(void*);void* arg;};
