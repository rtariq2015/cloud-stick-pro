#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
struct walker_heap_into_t {intptr_t start,end;};
struct walker_block_info_t {void* ptr;size_t size;bool used;};
using heap_caps_walker_cb_t=bool(*)(walker_heap_into_t,walker_block_info_t,void*);
void heap_caps_walk(uint32_t,heap_caps_walker_cb_t,void*);
