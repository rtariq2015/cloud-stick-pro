# Saved Wi-Fi fallback update

Firmware **0.4.4-wifi-dev** is built and installed on the development dongle.
Linux desktop **0.5.4** is installed and recognizes that exact firmware version.
Android remains **0.1.2**. Earlier artifacts and recovery images are preserved.

The [Wi-Fi fallback preview](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.06-wifi)
contains the reviewed app-only image and Linux packages. The app image requires
an already compatible Pro layout; it is not a factory installer. Distinct-network
physical switching, native Windows, packaged-GUI device interaction, phone
acceptance and long endurance remain separate gates.

## Source and artifact identities

* Firmware build source: `d5d5f985e54b365b8441eb6e48392acf37288879`.
* Desktop build source: `eadf54f041dfd64e57f25972f3fc3181a0eb719c`.
* Frozen SDK manifest: `1667e4e5650d161240a3ba6a271f02004717ddc1caec56cf178805f8e0738237`.
* SDK profile header: `51f53e882e3e3b735e166ffa19b8a9580204c37c5ce7a256251538de76023976`.
* Partitions: 3,072 bytes, SHA-256 `35897a52e0d951707f219eb6292b945ad55095364e3a8075eeeb4b221e211798`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `cloud-stick-pro-0.4.4-wifi-dev-app.bin` | 1689808 | `b3a19ed7e00a0dd68b30b08c5efbac061a33089dc5256a8eae57068dd46bb579` |
| `cloud-stick-0.5.4-linux-x86_64.tar.gz` | 217742808 | `00da0d37119fc4ba8004c9dbeee08f8364b0415186c968d5adbbe78aa046b88a` |
| `cloud-stick_0.5.4_amd64.deb` | 217682630 | `489f15b8735e1b5c61f31ba1fc40ab610684e60a05ac2f3f9ddec8fe58a848fd` |

The guarded build verified all **6,509** pinned SDK files before and after the
build, source stability, the NimBLE backport, incremental storage boundary,
exact partitions and used USB buffers. No SDK substitution or rebuild occurred.
Static RAM remains **84,412 bytes**. The app is **64 bytes larger** than 0.4.3.
A generated factory image remains local; only the reviewed app image is released.

## Physical installation and checks

The app-only update followed safe host removal, stopped/reaped worker and a
released radio lease. Two fresh matching full **16 MiB** reads were encrypted
and independently reopened before any app write. The current 0.4.3 image and
layout/security conditions were checked. The single app write at `0x10000` was
independently read back; bootloader, settings/layout and receipt ranges matched
the checkpoint byte-for-byte before controlled normal return. No microSD format,
partition/eFuse write or credential change occurred.

Native USB health passed using unmodified desktop 0.5.4 protocol code and its
exact-version allowlist. A setup-window lifecycle test stopped/reaped the worker,
released its radio lease, then closed setup and recovered local Wi-Fi and the
worker on the same boot/USB enumeration. Configuration revision remained **7**.
A subsequent current-file scan reported **four verified, zero pending/failed or
backup errors**, with complete scan evidence. This is physical reconnection to
the saved Wi-Fi; switching between two distinct live networks remains unvalidated.

One new exclusive **2 MiB synthetic PDF** then passed automatic backup and the
dongle's cloud verification. A separate full TLS GET matched its original
SHA-256 (`bebe50d073467625827398cd1b84a930b2074df28f7e08bcf5e3cd9b96443110`),
with certificate/hostname validation, no redirects and no cloud write by the
verifier. The current scan finished with **five verified, zero pending/failed or
backup errors**, on the same USB enumeration with increasing uptime. The bounded
LAN observer had eight temporary unavailable reads during the trial; it did not
count those as passes. Final native health and dashboard state passed.

The post-transfer snapshot reported 165,612 bytes free internal heap, 94,196-byte
largest block and 7,752 bytes worker stack free. Sampled minima were 40,380 bytes
free and 28,660-byte largest block, with no observed allocation failure. The
29,712-byte historical per-region TLS watermark is a different measurement;
sampling cannot exclude a shorter unobserved dip. There is no physical counter
of old-file content reads and no new upload-speed claim.

Linux 0.5.4 passed **151 Python** and **121 frontend** cases, native packaging,
both archive checksums, all **3,008** installed regular-file hashes and installed
Qt smoke. The prior user installation is preserved. This is separate from a
packaged-GUI physical interaction test. Windows recipe/version is updated; no
Windows binary or acceptance is claimed. Existing Android 0.1.2 is unchanged.

## Source regressions

The eight-network regression failed against the original policy and passes with
the change. Historical, explicitly disabled and fair policy modes pass strict
ASan/UBSan/leak checks. Cases include eight retry rounds with 1-second and
25-second failures, cooldown saturation, clock rollover, repeated polls, disabled
profiles, invalid counts, success/reset, priority ties and no-setting mutation.

The real owner-loop/NVS adapter suite passed **53 records: six builds and 47
simulated runs**, including slow eight-network failover and worker retirement.
These use controlled radio, worker, USB, BLE, NVS, clock and heap fixtures; they
are not hardware or real cloud tests. The full guarded-builder suite passed
**53 tests** against the retained reviewed inputs. Actual feature-header admission,
32 publication-checker tests and eight source-export tests also passed.
The private operator passed 34 synthetic recovery/ordering cases; its read-only
status adapter passed seven and synthetic PDF/TLS adapter passed nine.

```sh
python3 tools/test_wifi_reconnect.py
CSP_TEST_CLANGXX=/absolute/path/to/clang++ python3 tools/test_backup_runtime.py
python3 test/product_rc/profile.test.py
```

## Behavior

The existing Pro setup firmware retains up to eight enabled/disabled saved
networks. An owner can add a network through the authenticated desktop wizard's
Find Wi-Fi, Check and Save flow; a different network does not replace the other
saved entries. Networks not explicitly saved are never automatic candidates.
The Android viewer does not save Wi-Fi credentials.

When Wi-Fi disconnects, the owner loop retires and reaps the backup worker before
starting another connection. A successful local link is kept. The existing retry
order prefers the selected network, then its existing priority/ID ordering,
subject to a 10-second to five-minute exponential cooldown.

A connection attempt can take 25 seconds, longer than the first cooldown. The
old policy could therefore retry a preferred failed network before all saved
alternatives had a turn. The new opt-in policy first considers the eligible
networks with fewer consecutive failures, then applies the existing ordering.
It uses the existing eight bounded failure records, without extra persistent
state, allocation or saved-setting writes. A working link does not trigger
switching. Configuration revisions reset retry history; a successful connection
clears that profile's failures. Cooldowns still apply when all networks fail.

This handles Wi-Fi disconnection. Remaining connected to an access point whose
Internet service is down does not trigger automatic network switching. A captive
portal may still need the owner's acceptance. Local association does not prove
Internet access or a verified cloud backup.

Historical/default profiles keep the previous retry policy. The new candidate
inherits the UI/incremental profile and enables `CSP_ENABLE_WIFI_FAIR_RETRY=1`
under its own version identity. Partitions, USB storage ownership, receipts,
authentication, TLS validation and worker shutdown requirements are unchanged.


## Preparation failures and remaining limits

The original source-only milestone lacked a bound frozen SDK. The retained
reviewed SDK was later located and bound read-only beside the canonical source;
all original guard checks passed. No old product worktree was rebuilt.

An initial operator launch used the wrong wrapper working directory and stopped
before any serial child or flash write. Correcting that invocation retained all
recovery/security checks. Desktop preparation also exposed missing native EGL
bindings and test-directory permissions; the pinned environment and private-test
umask resolved them. Packaging under that private umask failed its Debian control
directory permissions; a full rebuild under the standard packaging umask passed.
An offline derived-cycle script newline error was corrected before device use.
The independent cloud verifier also stopped before opening recovery data because
its observed swap limit did not satisfy the retained gate. Recreating the
restricted services inside the zero-swap scope and checking their observed limits
resolved it. A host/container path binding was corrected with its source hash
still pinned. No protection check was bypassed. These preparation failures are
retained locally and are not hardware passes.

The earlier GCC optimized ArduinoJson warning and unused owner-window helper
were resolved with the intended Clang test environment and feature-conditional
helper compilation. Strict warnings and sanitizers remain enabled.

The first source milestone's SDK check correctly failed closed; build and physical
evidence above supersede that pending state. The initial five configuration tests
used a synthetic board-reference fixture; the later full guard suite and actual
SDK manifest verification are distinct evidence.

Eight-network retry order is proved by source simulation. Actual distinct-network
failover, Internet-loss while still associated, venue portals, multi-day endurance
and a physical 100-file workload are not claimed. No new raw upload-speed increase
is claimed. Setup-close reconnection does not itself prove a cloud upload.
The [credit and source/binary gates](../CREDITS.md) remain unchanged.
