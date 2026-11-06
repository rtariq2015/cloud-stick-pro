# Pinned BLE SDK corrections

This directory retains exact official ESP-IDF v5.5.5 transport and private header,
and the exact esp-nimble task adapter at commit
`685675c0128deafdd201c9eb82e61d227364646c`. The unmodified Security2 input is
retained in `test/milestone1/sdk_vendor/security2.c`. See `PROVENANCE.json` for
source URLs and Git blob hashes. Apache-2.0 license text and upstream headers are
retained. No framework upgrade is made.

`tools/pinned_ble_sdk.py` verifies all original inputs, applies exact-context
transformations in memory and rejects differences in the checked-in corrected
sources or corresponding review patches. `--write` reproduces those files.
Firmware and desktop tests both compile `patched/security2.c`.

* The transport propagates the real `nimble_port_deinit()` result and requires
  host-task exit plus `ESP_BT_CONTROLLER_STATUS_IDLE` before freeing its owned
  state. Deinit failure retains transport ownership, and application shutdown
  remains blocked. A shared failure latch prevents a second destructive deinit
  when startup cleanup has already failed. A partial init failure with a nonidle
  controller is retained without guessing which additional SDK stop is safe.
* The task adapter checks task allocation. Application startup waits at most two
  seconds for the actual host sync/address/advertising callback result. A pending
  task whose handoff is revoked on timeout exits when it is eventually scheduled,
  without entering the SDK. Cleanup waits at most one second for its exit ack;
  if that cannot be established, ownership remains retained and denied. No reset
  or forced deletion of a running task is used. The ordinary pinned host-stop
  procedure itself is unchanged; its stop semaphore waits are SDK behavior.
  A task paused between entering its trampoline and entering the SDK event loop
  has a separate acknowledgement; revoking that handoff also prevents late SDK
  entry. The host samples its own minimum remaining stack immediately before
  exit; the owner reads an atomic byte count, never a deleted task handle.
* Security2 accesses the existing 12-byte IV's big-endian 32-bit counter through
  bytes. This removes alignment-requiring struct casts while preserving the exact
  counter encoding, increment/wrap guard, SRP flow, proofs, AES-GCM calls and
  Security2 version/patch. There is no sanitizer suppression. The IV/session
  layout is unchanged.
* The enabled image includes the pinned Arduino
  `esp32-hal-alloc-ble-mem.h` constructor. It reserves BLE memory before
  `initArduino()` can irreversibly release it. The ELF check reads the actual
  initializer array to verify the constructor is included; the disabled build
  must omit it.
* The isolated-only `ble_sdk_volatile_store.c` implements the exact no-argument
  persistence ABI from pinned `ble_store_nvs.c`. The SDK's existing RAM store and
  callbacks remain in use, but prior bonds/IRKs/CCCDs are never loaded and their
  updated values are never persisted. These success returns acknowledge RAM
  updates only, consistent with this deliberately nonpersistent image. No
  synthetic credentials or BLE ownership state are written to NVS. This is
  required because the pinned SDK has `CONFIG_BT_NIMBLE_NVS_PERSIST=1` even when
  application bonding is disabled. Generic Arduino NVS/PHY initialization is
  separate and subject to the isolated image's write barrier.

Only source files guarded by `CSP_ENABLE_BLE_MANAGEMENT` include these corrections.
They provide the strong transport/task/security symbols before the pinned SDK
archives. The enabled firmware link map must show these source objects and must
not select the archive members `protocomm_nimble.c.obj`,
`nimble_port_freertos.c.obj`, `security2.c.obj` or `ble_store_nvs.c.obj`.
`tools/verify_ble_sdk_link.py` enforces these exclusions and the strong symbol
providers, saves representative target disassembly, and checks BLE reservation
in the actual ELF initializer array. The default build compiles empty
guarded translation units and must retain no enabled BLE runtime symbols or
memory-reservation constructor. The default framework can select its stock
Security2 archive member through other Arduino dependencies before garbage
collection; its discarded input sections are not enabled BLE runtime code.

Desktop fault injection can establish return propagation, acknowledgment policy
and late-task handling. It does not establish physical RF, RTOS latency, USB,
heap recovery, task cleanup timing or device readiness.

Reproduce source/desktop checks inside the development container:

```sh
python3 tools/pinned_ble_sdk.py
python3 test/milestone1/build_security2_host.py --sanitize
python3 test/milestone1/test_security2_host.py
python3 test/milestone1/test_sdk_lifecycle.py
python3 tools/verify_ble_sdk_link.py --env tdongle_s3_ble_m1_isolated
python3 tools/verify_ble_sdk_link.py --env tdongle_s3 --disabled
```

The lifecycle test uses verbatim extracted corrected C startup, sync, advertise,
stop and task-creation functions plus the actual C++ lifecycle implementation.
Its fake RTOS/controller effects include 16 startup/shutdown fault scenarios,
late entry/loop revocation, and repeated-call checks. The RAM-store test executes
the original initializer with desktop callback/counter stubs (its static-memory
branch), and the counter test checks eight unaligned carry/wrap boundaries.
Strict nonrecovering ASan/UBSan settings and stderr rejection are enabled.
