#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_INTERNAL=1u<<11;
constexpr uint32_t MALLOC_CAP_8BIT=1u<<2;
size_t heap_caps_get_largest_free_block(uint32_t);
