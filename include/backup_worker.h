#pragma once
#include <cstdint>
#include "ble_feature.h"

#if CSP_ENABLE_BACKUP_SETUP
struct CloudConfig;

enum class BackupWorkerPhase : uint8_t {Stopped,Starting,Running,Retiring,Drained,Failed};
struct BackupWorkerSnapshot {
  BackupWorkerPhase phase=BackupWorkerPhase::Stopped;
  uint32_t generation=0,retired_generation=0;
  int error=0;
};

// These control calls belong to one pinned owner task (the Arduino loop task).
// A successful start admits exactly one immutable config and clears maintenance.
// Failed admission leaves maintenance enabled; call reap before retrying a Failed
// allocation. A retirement request is idempotent for its current generation.
bool backup_worker_start(const CloudConfig&);
uint32_t backup_worker_request_retire();
BackupWorkerSnapshot backup_worker_snapshot();

// Drained means worker C++ scopes have unwound, not that stack memory is free.
// Reap requires a live, proven self-suspended task on this owner's pinned core.
// It removes that task before freeing its caller-owned static stack/TCB. Only a
// successful reap and Stopped state permit the caller's separate heap gate.
bool backup_worker_reap();

// Pinned registered owner only, after complete drain/reap. Call after any
// durable cloud/config operation that can invalidate receipt/scope proof.
// No media/metadata writes; feature-off builds still enforce owner/stopped.
bool backup_worker_invalidate_scan_cache();
#endif
