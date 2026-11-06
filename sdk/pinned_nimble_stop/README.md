# Project-side NimBLE shutdown correction

The observed physical lab4 panic was `npl_freertos_eventq_remove` during
`nimble_port_stop`, with an intact 16-PC trace. The exact image and decoded
locations are preserved under `artifacts/panic-diagnostics/physical/`. A second
authenticated setup session reproduced it after an open-network check; it is
not specific to public AP routing.

`INPUTS.json` pins two original C translation units and their private host
headers. The originals match the frozen SDK and official Espressif NimBLE
revision `685675c0128deafdd201c9eb82e61d227364646c`. The project includes its
patched copies through two separately gated source providers. The SDK packages
and their 6,509-file integrity manifest remain untouched.

Two official changes guide this backport:

- [Shared event-queue synchronization, bdc5010](https://github.com/espressif/esp-nimble/commit/bdc5010548e988a770adb8af107d01feb850c0ca).
- [Host timer lifetime, 1a714b0](https://github.com/espressif/esp-nimble/commit/1a714b03dcea55e58066e21213a5f150f2e50088).

Espressif also explains the separate in-flight timer problem in
[IDFGH-18599](https://github.com/espressif/esp-idf/issues/19348). Removing a
queued event alone cannot protect one the host has already dequeued. This
backport stops the host timer during stop, prevents rescheduling while disabled,
and retains destruction in the existing final `ble_hs_deinit`, after host exit.

The reduced queue patch uses one shared recursive task mutex and the existing
shared port spinlock. Blocking peeks and finite mutex waits happen outside
critical sections. Task and ISR queue operations update pointers and queued
flags atomically under the same spinlock. Nested NPL critical depth is tracked
per core; an already-critical caller does not wait for a mutex. Callback execution
remains outside these locks. Finite GET timeouts retain their total tick budget,
including wrap and stolen wakeups.

Unlike the broader upstream queue change, this patch retains genuine insertion,
dequeue and requeue assertions. It does not drop full-queue events or report
failed insertion as queued. Wrong-queue removal and blocking GET within an NPL
critical section are rejected. The added mutex is created/deleted with the NPL
pool lifecycle; failure to allocate that new mutex returns an init error before
any pool has been touched. Existing SDK allocation checks remain in place.

Run `python3 tools/pinned_nimble_stop.py` to verify the exact transformation;
`--write` regenerates only these project copies and patches. No SDK mutation,
archive modification, duplicate-definition override, watchdog change, affinity
change, Security2 change, USB callback change, partition change, SD operation or
receipt-format change is part of this correction. Only the explicit lab5 profile
enables it. Final product enablement must follow physical acceptance.

The exact ELF/map must prove both strong providers and absence of the original
`npl_os_freertos.c.obj` and `ble_hs.c.obj` archive members. The extended
`tools/verify_ble_sdk_link.py --env tdongle_s3_product_lab5` checks this.

`test/nimble_stop/run.py` compiles actual old/new extracted function bodies with
fatal ASan/UBSan and leak checks. Three old failures reproduce; fixed variants
exercise deterministic logical cross-core interleavings, task/ISR paths,
duplicate enqueue, finite/wrapping timeouts, critical nesting, in-flight timer
lifetime, repeated cleanup, new-mutex allocation failure and default-off policy.
These are simulated adapters, not real SMP scheduling or target ISR timing.
Physical repeated sessions, memory recovery, USB continuity and provider ABI
acceptance remain required before claiming the device fault is fixed.
