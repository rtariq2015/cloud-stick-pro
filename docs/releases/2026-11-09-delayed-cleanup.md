# Delayed Bluetooth cleanup candidate — 9 November 2026

Private firmware **0.4.12-close-dev** is being prepared after a real 0.4.11
session reproduced “BLE needs review.” This is a bounded recovery candidate,
not a released fix or evidence that the work guest network now connects.
[Physical observations](2026-11-09-guest-wifi-diagnosis.md) record the exact
installed image, failure, later natural memory recovery and normal restart.
The remaining allocation's caller has not been identified.

## What changes

After confirmed Bluetooth stop, Wi-Fi driver release and successful unchanged
NVS verification, this candidate can wait **up to four minutes** for the original
full contiguous-memory proof. It keeps servicing USB and the watchdog, leaves
the backup worker stopped and shows **Waiting for cleanup** after 30 seconds.
Memory that returns quickly follows the existing immediate path.

Only a successful proof permits one Wi-Fi restart. That restart gets its own
existing 30-second settlement window, so late memory recovery does not consume
its connection time. The combined wait is bounded by four minutes plus thirty
seconds. A failed stop, failed NVS verification, failed reinitialization or
expired deadline remains blocked. Later heap changes cannot reopen that failure.
No SDK allocation is forcibly freed, memory threshold lowered, reset sent,
settings changed or file/receipt check bypassed.

The new feature flag defaults off and is admitted only in this separately
versioned development candidate by the guarded build. Historical environments
retain their prior timing. Normal downloads and update feeds stay unchanged.

## Software validation

A regression against the previous runtime first failed because the old closing
window expired at 30 seconds. The new actual owner-loop tests use simulated
USB, radio, worker, clock, heap and NVS. They exercise delayed recovery, a
four-minute deadline that stays latched, clock rollover, a full separate network
window after late recovery, network timeout, and unchanged settings writes.
The same cases run with the real NVS-release source, including cache-init,
cache-deinit and flash-equality failures. Other owner-loop and protocol scenarios
remain in the complete sanitizer runner. Hardware acceptance remains pending.

The static clock and timeout changes do not identify the underlying SDK object.
This candidate tests safe waiting for observed natural recovery; it does not
establish a guest-network, enterprise-network or cloud-upload fix. Enterprise
Wi-Fi still requires a separate supported profile and verified server settings.
Private recovery copies, credentials and raw device logs are excluded. The
source/binary publication gate remains in [Credits](../CREDITS.md).

## Exact software/build milestone

All **97 scenarios across ten sanitizer builds** passed, including both new
settlement paths and the historical runtime paths. All recorded source hashes
matched. The 62 build-guard self-tests, three radio-source modes, 35 feature
admission cases and 36 publication tests also passed. The public guard admits
1,084 reviewed files. Runtime worker/radio/NVS/heap behavior is simulated in
these checks; they do not establish physical acceptance.

The exact target build source is
`68aa76ea2f334d710d65c41c1a6566600dc5602d`.
Application: **1,704,880 bytes**, SHA-256
`15d9c114673ae9b9c70526f1a283b2ee154cc3f3041c71529208c798f8c614b2`.
Static RAM: **84,868 bytes** (+8 versus the retained 0.4.11 build); application
size increased by 128 bytes. All 6,509 frozen SDK entries matched before/after,
with unchanged partitions and the existing USB/storage review gates passing.
A private signed package was verified under actual no-swap/core-dump-disabled
conditions. It has not been installed or put into the normal catalog/feed.
