#pragma once
#include <cstddef>
#include <cstdint>
#define ESP_OK 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
using esp_alloc_failed_hook_t=void(*)(size_t,uint32_t,const char*);
int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t);
size_t heap_caps_get_free_size(uint32_t);
size_t heap_caps_get_largest_free_block(uint32_t);
