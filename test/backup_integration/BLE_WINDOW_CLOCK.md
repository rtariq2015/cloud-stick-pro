# UART BLE-window clock correction

2026-11-04. **Source/native gate: PASS. Physical correction test: pending.**
No device/UART, SDK/target build, installer, phone, WebDAV or frozen candidate
artifact was changed by this lane. The integration owner is responsible for
the combined source review and a new image-specific physical test.

## Cause of the persistent Drained observation

The owner loop captured `now` before `serial()`. The UART `BENCH_WINDOW`
handler opened a lifecycle window with a newer `millis()`. The old lifecycle
Waiting timeout subtracted that new `openedAt` from the older loop sample
using unsigned arithmetic. A one-millisecond ordering difference therefore
looked like nearly 49 days and immediately crossed the genuine 45-second
quiescence limit.

At that point the worker could correctly remain Retiring while cancelling a
PUT. The Stop branch found it unreclaimed, set `backup_bridge_fault`, and
placed the lifecycle in Blocked. When the worker subsequently became Drained,
the owner stopped calling the reap path: the Waiting branch was no longer
active, resume/reconnect rejected the latched fault, and an idle/completed
public coordinator returned before its own reclaim observation. Consequently,
Drained remaining visible did not demonstrate a FreeRTOS failure to suspend or
delete the task; the owner had stopped attempting the safe reap.

Relevant locations are `BleLifecycle::requestWindow/poll` in
`include/pro_ble_lifecycle.h`; `serial()` BENCH_WINDOW, the Waiting
`backupReclaimed()` call and the failed-Stop branch in
`src/wifi_setup_runtime.cpp`; and `pauseBackup`, `backupReclaimed`,
`resumeBackup` in that runtime.

The existing worker guard remains intact: `src/sync_engine.cpp` publishes
Drained only after worker scopes unwind, then self-suspends. Reap requires the
correct pinned owner/core, matching generation, no metadata error and actual
`eSuspended` before deleting/freeing. The pinned FreeRTOS evidence and existing
worker tests distinguish a Drained acknowledgment from the scheduler's parked
state. This change does not bypass any of those checks.

## Narrow correction

- `BleLifecycle` now uses a half-range safe age for hold, quiescence and active
  window deadlines. Backward/stale samples have age zero; forward uint32
  rollover retains its correct small elapsed value.
- The owner calls `lifecycle.poll(millis(), ...)` after serial dispatch, so a
  newly opened UART window is polled with a current timestamp.

The physical three-second hold, 45-second quiescence limit, two-minute setup
window and all heap/transport/ownership checks are unchanged. All deadlines are
below half the uint32 clock range, with the normal assumption that a phase is
polled within half a millis cycle. A genuine timeout with an unknown worker
still stays blocked. There is no automatic unblocking, forced task deletion,
USB reinitialization or reset in this correction.

## Evidence

`artifacts/ble-window-clock/results.json` reports **17 passing command records**
with source hashes unchanged throughout the final run. SHA-256:
`0476964f8c5ea4e5c440ae34a7931ee18c2effb94baf65909ebb9adafadd0869`.
Strict ASan/UBSan and leak detection are enabled; no sanitizer diagnostic can
pass. Successful commands have empty stderr. Expected old assertion failures
are explicitly marked as negative tests.

The exact old source from commit
`076180b7630f157b770f7716ae9400bf50ae33c9` fails both the stale-time pure policy
test and the actual UART/owner-loop test, which prints that the UART window
became blocked before a valid later reap. Tests prove each correction
independently: fresh owner polling passes with the old header, and the guarded
header passes with the old owner call.

The fixed code passes:

- Five policy groups for stale samples, hold boundaries, exact real deadlines,
  rollover and unchanged memory/transport denial.
- Four actual owner/UART scenarios: normal, rollover, three repeated same-boot
  cycles, and a genuine 45-second unknown-worker timeout. They exercise delayed
  Retiring → Drained without parking, stale retirement generation, a correct
  reap before BLE, no post-reap stale queries and successful same-boot backup
  resumption. NVS remains unchanged and USB initializes exactly once.
- The existing lifecycle/parser suite: **719 checks**, **100,000 hostile
  messages**, **100 lifecycle sessions**.

RTOS worker/USB/radio/NVS are synthetic in the owner-runtime tests. They link
the actual serial handler, owner loop and lifecycle policy; they do not perform
real TLS or a 32 MiB upload. The common fixture headers were not altered.

## Reproduce

```bash
./compose.sh -f docker-compose.yml \
  -f build/research/cloud-stick-auth-cleanup-compose.yml \
  exec -T --user 1000:1000 \
  --workdir /home/home/tool.plugin-builder/build/research/cloud-stick-public-wifi-stage \
  unmanic-dev python3 test/backup_integration/lifecycle_clock_tests.py
```

The refreshed run includes the integration owner's final 38-field
`UPLOAD_STATUS` handler. Both this gate and the separate
`artifacts/upload-uart/results.json` gate identify the tested runtime SHA-256 as
`21ee15e4fa86044b85d6742c1c8aebd12ff1baa24ed09fa98172e9df74014a2c`.
A combined final regression/build snapshot still belongs to the integration
owner; these native tests do not claim target or physical acceptance.

## Already-blocked running firmware

This source fix does not clear a fault already latched in the running device.
Keep maintenance asserted. Do not free a task merely because UART says Drained,
resume backups/radio on unknown cleanup, or interrupt host-owned storage.
Recovery needs the integration owner's separate, reviewed safe-host-removal
and reset/recovery procedure, or a reviewed terminal cleanup path retaining
the actual park/generation/metadata proof. No such recovery action or
auto-unblocking path was added here.
