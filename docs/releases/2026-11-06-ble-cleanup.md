# Bluetooth cleanup and saved Wi-Fi acceptance

The timing-only **0.4.5-ble-dev** candidate failed the owner's authenticated
Wi-Fi Save/Finish trial. The **0.4.6-cache-lab** service image has now passed real
settings-cache stress, repeated Bluetooth closure and a controlled round trip
between two saved Wi-Fi profiles. The normal **0.4.6-cache-dev** image is now
installed and has also passed repeated Bluetooth closure and resumed backups.
The owner's exact authenticated phone Save/Finish flow remains unverified.

Linux **0.5.7** is installed and adds **Diagnose my stick** guidance for Bluetooth
cleanup, paused backups, missing Wi-Fi and incomplete scans. Android remains
**0.1.2**. The normal image is prepared as a development preview; the service image is
never a download.

## What failed on 0.4.5

The authenticated hotspot Check/Save/Finish advanced the saved revision to **9**,
but the unchanged memory proof failed after **30,001 ms**. The pre-setup largest
block was **118,772 bytes**; release remained at **61,428 bytes**, despite
**220,460 bytes** total free. BLE and the driver had stopped, the worker was
reclaimed and the power lease released. Five verified files and zero backup
failures remained recorded. Extending the wait was insufficient; no 0.4.5 binary
download was published.

A separately gated, read-only ROM inspection verified the existing app, captured
two matching encrypted/reopened settings reads, and returned normally without a
flash/settings write. Allocator metadata showed a 144-byte physical block,
consistent with a 128-byte allocation plus metadata, splitting the large free
area. NVS uses 128-byte HashList cache blocks. The historical allocation's owner
was not proven: SDK shutdown had already run before the inspection. The recovered
stick retained revision 9, connected Wi-Fi and five verified files.

## The checked cache correction

The opt-in 0.4.6 profile rebuilds the default NVS RAM cache only after verified
BLE/controller shutdown, Wi-Fi driver release and worker reclamation. It takes a
private snapshot of the unchanged 20 KiB NVS partition, uses the public SDK
deinit/init calls, and compares every flash byte before proceeding. Temporary
copies are scrubbed. Any partition, allocation, read, SDK or equality failure
stays blocked. The original contiguous-block proof, final heap thresholds and
30-second deadline remain unchanged. There is no automatic NVS erase, direct
free of SDK pointers, measurement rebasing or management-authentication change.

The separate service image has a fixed public diagnostic namespace. Its fixture
performs at most 32 public integer updates, once per boot, with no caller-selected
namespaces, keys or real profile values. A separate command removes only that
public probe and verifies its absence. These commands require an ejected host
volume, reclaimed worker, active bench window and no connected BLE peer. The
normal image is checked for absence of fixture commands and data. The lab binary
is private and must never be offered as an update download.

## Physical service-image tests passed on 7 November

The protected app-only install passed two fresh matching full 16 MiB reads,
encrypted independent reopening, exact preimage/security/layout checks, a single
app write at `0x10000`, independent app readback and byte-for-byte preservation of
the bootloader, settings/layout and receipts before normal return. Host storage
was safely ejected; serial ownership and child-process quiescence were checked.

The real SDK fixture accepted **32** updates. During close, rebuilding NVS
increased the largest block from **69,620** to **139,252 bytes**; every NVS flash
byte remained equal. This exceeded the **131,060-byte** pre-window baseline.
Release settled in **503 ms**, the unchanged final memory proof passed, Wi-Fi
returned and backup worker generation **2** completed its scan with five verified,
zero pending/failed files and zero worker/backup errors. This demonstrates that
retained NVS cache allocations can prevent the contiguous-memory proof and that
the public cache lifecycle can restore it on the actual SDK/board.

The synthetic probe was removed and a second close also passed: largest block
**118,772 bytes** before and after cache release, **503 ms** release settling,
worker generation **3**, five verified files and no errors. The diagnostic
namespace catalogue entry can remain; the public probe value is absent.

Two deliberate local driver disconnects then produced connections to distinct
saved profiles and back, with backup worker generations **4** and **5**. Each
new connection was observed across two healthy snapshots. Saved revision **9**,
two stored profiles, five verified files, zero pending/failed or worker/backup
errors and continuous uptime were retained. This was a real controlled local
link-loss test with both access points available, not an access-point power-off
or real outage test. It ran on the exact private service image below.

The fixture uses no personal GATT login and does not perform the owner's exact
authenticated Save/Finish flow. That acceptance remains required. No new cloud
upload, packaged-GUI hardware session, exact-version phone trial, venue portal
or long endurance acceptance is claimed.

## Normal-image acceptance and saved-settings comparison passed

The normal image passed the same protected app-only installation gates, including
two fresh full matching encrypted/reopened recovery copies, independent app
readback and unchanged bootloader/settings-layout/receipts. It contains neither
the synthetic fixture nor the local link-drop commands.

The first acceptance attempt stopped before opening BLE because an early startup
backup status was nonzero. The adapter did not retain its exact numeric code,
so no specific cause is inferred. A later read-only check reported both worker
and backup errors zero, a complete scan and five verified files at 134 seconds
uptime. The strict checks were retained. A manually reviewed retry from that
healthy baseline passed both closes; failure text now retains numeric codes.

The normal-image first close restored the exact **122,868-byte** largest-block
baseline; the second restored **118,772 bytes**. Both release waits were **503 ms**,
NVS comparison reached verified with SDK error zero, and the original memory
proofs passed. Backup worker generations **2** and **3** resumed, with complete
scans, five verified files, zero pending/failed and zero backup/worker errors.
Saved revision **9**, both stored profiles, selected profile and continuous
uptime were retained. No allocation failure was recorded.

An offline comparison independently reopened both encrypted full-read pairs
from before the service trial and before the normal installation. All logical
NVS values were identical, their entry/data CRCs verified, and the synthetic
probe was absent. Only the expected public diagnostic namespace catalogue entry
was added. The normal installation then preserved settings flash byte-for-byte,
and both normal closes separately verified NVS byte equality. No private values
or raw device logs were published.

## Exact source and artifacts

Firmware source: `adc439bcc69802d289fa79b34f9994f4093d0f83`,
[PR #8](https://github.com/rtariq2015/cloud-stick-pro/pull/8).
Desktop 0.5.7 build source: `90f632d7ee7d39be4a142aa7b842eddba68f57ab`,
[PR #9](https://github.com/rtariq2015/cloud-stick-pro/pull/9).
The original failure remains tracked in [issue #5](https://github.com/rtariq2015/cloud-stick-pro/issues/5).

| Build artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Normal 0.4.6 app, installed and accepted | 1691232 | `b0d5984282d5ed41f2dfa99558aca38aae8b865102d959de3ff61118228554a3` |
| Private 0.4.6 service app, never a download | 1692944 | `2727dbe76c09664e0455bf8e481a0d7b457294645efb1b95d4b841e292849bb0` |
| `cloud-stick-0.5.7-linux-x86_64.tar.gz` | 217742683 | `fb5459ebc4c06d4535341c6e8d0302786b313b8bb4bfef0d61b2111e1e596548` |
| `cloud-stick_0.5.7_amd64.deb` | 217683566 | `06b816c471fd951ec65409480c23244c0da530f4d12a71a1f74a1da545e3b512` |

Both guarded builds checked **6,509** frozen SDK files before and after, source
stability, the pinned NimBLE stop adaptation, eight reviewed USB/storage sources,
used USB buffers and exact partition bytes. Static RAM is **84,436 bytes**, 24
bytes above 0.4.5; the normal app is 1,392 bytes larger. Default profiles, USB
ownership, files, authentication, TLS verification, watchdogs and layout remain.

* Normal guarded-build record: `b4f75c9e17058c018cf929b052f26ddc056120e513a7dc6b4aaf8a5c740f7ef0`.
* Service guarded-build record: `77cd2d8393e7d82e6843cf6d816205ce151adbb918c9d8bf4cd62ca066025157`.
* Frozen SDK manifest: `1667e4e5650d161240a3ba6a271f02004717ddc1caec56cf178805f8e0738237`.
* SDK profile header: `51f53e882e3e3b735e166ffa19b8a9580204c37c5ce7a256251538de76023976`.
* Partitions: 3,072 bytes, `35897a52e0d951707f219eb6292b945ad55095364e3a8075eeeb4b221e211798`.

## Local and packaged application verification

**78** owner-loop build/run records passed with simulated hardware and address,
undefined-behavior and leak sanitizers. **25** strict cache-release/fixture
records passed with synthetic SDK/flash, covering cleanup errors, exact flash
comparison, snapshot scrubbing and bounded fixture behavior. **56** build-guard
cases, **151** desktop Python cases, **127** desktop frontend cases, **32**
publication cases and **8** exporter cases passed. CI passed for the proposed
source. These simulations are separate from the physical trials above.

The private updater passed **34** synthetic recovery/ordering cases; the physical
trial adapter passed **7** offline guard cases, including rejection of a same
profile reconnect, old worker generation or failed memory proof. No hardware
result is inferred from those tests.

Desktop 0.5.7 passed native Linux packaging, all **3,008** installed regular-file
hashes and the installed Qt startup smoke test. The prior installation was
preserved and saved app settings were unchanged. Diagnose my stick uses the
existing allowlisted local snapshot, explains a reported Bluetooth cleanup
failure with a useful next step, and preserves the safe report export. It adds
no automatic repair or device commands. Windows recipe/version is updated;
no native Windows binary or acceptance is claimed. Attribution and component
notices remain in [CREDITS](../CREDITS.md).
