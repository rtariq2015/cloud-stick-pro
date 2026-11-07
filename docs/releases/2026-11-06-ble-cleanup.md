# Bluetooth cleanup correction

Firmware **0.4.5-ble-dev** is installed on the development dongle after a
protected app-only update and independent readback. Linux setup **0.5.5** is
installed and recognizes that exact version. Authenticated setup-close and real
network switching acceptance remain pending. Android remains **0.1.2**; no new APK is needed for this correction.

## Observed failure and change

After the owner saved a phone hotspot and pressed the final setup action,
`0.4.4-wifi-dev` displayed “BLE needs review” and left backups paused. Read-only
native diagnostics showed a durable configuration revision of **8**, verified
Bluetooth/driver shutdown, a stopped worker and a released power lease. The
release proof sampled a **63,476-byte** largest block after **2,001 ms**, below
the **118,772-byte** pre-setup value. A later snapshot showed the full
118,772-byte block had returned. Five verified files and zero backup failures
remained recorded. No Wi-Fi credentials or raw device logs are published.

The old release gate permanently failed after two seconds even if queued SDK
cleanup completed later. The opt-in candidate now lets that verified release
settle within the existing **30-second overall closing deadline**. Wi-Fi starts
only after the original full contiguous-block proof. Backups still require the
original final heap thresholds and a settled radio. USB polling continues during
cleanup. A failed stop/release/reinitialization or expired deadline stays blocked;
late memory recovery cannot silently reopen a terminal failure. Historical
profiles retain their original timing. No USB/storage, partition, watchdog,
authentication, TLS or receipt changes are included.

## Source and artifacts

Firmware and Linux build source: `aec31e7709dec9ee1a893a7cd871a109bb26a571`.
The source fix was merged through [PR #6](https://github.com/rtariq2015/cloud-stick-pro/pull/6),
with relevant CI passing. The original report is
[issue #5](https://github.com/rtariq2015/cloud-stick-pro/issues/5).

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `cloud-stick-pro-0.4.5-ble-dev-app.bin` | 1689840 | `d9579044bf9df00e2a80149f89162a486dfbef7ba30cf8ea5f01c719ee94734c` |
| `cloud-stick-0.5.5-linux-x86_64.tar.gz` | 217742868 | `262c76f2b6de954f4f4716f907262982548a6ac5cde3b7c45e81652ad6d341f3` |
| `cloud-stick_0.5.5_amd64.deb` | 217683662 | `cf0332ded7777f8714a49baefd975e396d7ce7ae5c29478f66cf17082d21bebc` |

The guarded build verified **6,509** frozen SDK files before and after the
build, source stability, the pinned NimBLE stop adaptation, eight reviewed
USB/storage sources, used USB buffers and exact partition bytes. Static RAM is
unchanged at **84,412 bytes**; the app is **32 bytes larger** than 0.4.4.

* Guarded build record SHA-256: `ce2c72fe2802c44387402fcca657044a278832f683f9a38e4efd553a778b8f40`.
* Frozen SDK manifest: `1667e4e5650d161240a3ba6a271f02004717ddc1caec56cf178805f8e0738237`.
* SDK profile header: `51f53e882e3e3b735e166ffa19b8a9580204c37c5ce7a256251538de76023976`.
* Partitions: 3,072 bytes, SHA-256 `35897a52e0d951707f219eb6292b945ad55095364e3a8075eeeb4b221e211798`.

## Verification completed before device installation

The delayed-return regression failed against the old implementation. The actual
owner-loop suite passed **64 records: seven builds and 57 simulated runs**, with
ASan, UBSan and leak checks. New scenarios cover memory returning after two
seconds, an unchanged 30-second failure deadline, no late automatic recovery,
clock rollover, continued USB polling and no Wi-Fi/worker restart before proof.
Stop, driver release and reinitialization failures remain blocked. Existing
repeated setup, same-profile durable save and Wi-Fi retry cases also pass.
These are controlled BLE/radio/worker/NVS/clock/heap fixtures, not hardware tests.

The build guard passed **54 tests**. Desktop passed **151 Python** and **121
frontend** cases, native Linux packaging, all **3,008** installed regular-file
hashes and installed Qt startup smoke. The previous installation was preserved.
Exact near-miss firmware versions remain refused. Windows recipe/version is
updated; no native Windows binary or acceptance is claimed.

The private installation adapter passed **34 synthetic recovery/ordering tests**.
Two additional tests execute its generated entry against synthetic serial/time,
including 26 uncertain worker, memory, release, identity and power states that
must prevent ROM entry. These tests do not prove physical hardware behavior.

## Hardware acceptance

The protected updater passed safe host removal, two fresh matching full 16 MiB
recovery reads, encrypted independent reopening, exact 0.4.4 preimage and
security/layout checks, one app-only write at `0x10000`, independent app readback
and byte-for-byte bootloader/settings-layout/receipt preservation before
controlled normal return. The already-stopped state was verified twice; the
procedure did not bypass the blocked lifecycle or reopen Bluetooth to enter ROM.

Read-only native health then reported 0.4.5, saved revision **8**, Wi-Fi locally
connected, backup worker generation **1** running, five verified files, zero
pending/failed or backup/worker/SDK errors, and no observed allocation failure.
The owner reported a normal screen, but no new BLE lifecycle had yet occurred
on that boot; that observation is not counted as authenticated close acceptance.

The first acceptance adapter stopped before opening BLE because its 75-second
startup-scan wait was too short. A later native snapshot at 133 seconds showed
the completed healthy scan. Its bounded wait was corrected to 180 seconds; no
firmware gate was relaxed. The next attempt stopped before opening BLE because
the computer had no protected setup-access bootstrap file. Personal sign-in is
required; no private login was requested, read from another source or bypassed.

Authenticated setup-close and same-profile save trials remain required. No new
cloud upload, distinct-network physical failover, packaged-GUI hardware session,
exact-version phone trial, venue portal or long endurance acceptance is claimed.
