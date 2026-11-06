#pragma once
#include <stdint.h>

// Volatile, read-only measurements from the owner loop. No persistent state.
struct BleMemoryStatus {
  uint32_t before_free, before_largest, after_free, after_largest;
  bool checked, stop_confirmed, memory_restored;
};
BleMemoryStatus ble_memory_status();
