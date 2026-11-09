// SPDX-License-Identifier: MIT
#pragma once
enum class BackupWorkerPhase {Stopped,Running};
struct BackupWorkerSnapshot {BackupWorkerPhase phase=BackupWorkerPhase::Stopped;};
BackupWorkerSnapshot backup_worker_snapshot();
