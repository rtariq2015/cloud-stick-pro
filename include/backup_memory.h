#pragma once
#include <cstdint>
#include "ble_feature.h"
#if CSP_ENABLE_BACKUP_SETUP
struct BackupMemorySnapshot {
  bool failure_hook_registered=false;
  uint32_t samples=0,observed_min_free=0,observed_min_largest=0;
  bool allocation_failure_seen=false;
};
// RAM-only observations. Sampled current totals are distinct from the SDK's
// sum of per-region historical minima; neither is a continuous trace.
// All public calls are owner-loop only. The allocator callback sets a sticky
// atomic flag; it does not take locks or count through a compare/exchange loop.
void backup_memory_init();
void backup_memory_poll(uint32_t now);
BackupMemorySnapshot backup_memory_snapshot();
#endif
