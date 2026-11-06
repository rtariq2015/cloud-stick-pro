#pragma once
// Reuse the existing SDK/NVS fixtures, replacing only the fixed heap reading
// so the actual owner loop can be exercised at its admission boundaries.
#define heap_caps_get_free_size runtime_fixture_fixed_heap
#include "../wifi_integration/stubs/common.h"
#undef heap_caps_get_free_size
extern size_t runtimeFreeHeap;
inline size_t heap_caps_get_free_size(int){return runtimeFreeHeap;}
void configTzTime(const char*,const char*);
