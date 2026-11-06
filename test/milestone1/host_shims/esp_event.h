#pragma once
#include "esp_err.h"
#include <stddef.h>
#define ESP_EVENT_DECLARE_BASE(name) extern const char *name
#define ESP_EVENT_DEFINE_BASE(name) const char *name = #name
#define portMAX_DELAY 0xffffffff
static inline esp_err_t esp_event_post(const char *base, int32_t id, void *data, size_t len, uint32_t wait) {
    (void)base; (void)id; (void)data; (void)len; (void)wait; return ESP_OK;
}
