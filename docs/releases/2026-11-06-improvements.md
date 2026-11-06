# Incremental backups and modern local companions

2026-11-06. Firmware, desktop and Android are separate components. This report
records completed tests and marks the next build/install gates explicitly.

## Current status

| Component | Completed evidence | Next gate |
| --- | --- | --- |
| Firmware 0.4.2-incremental-dev | Installed; two automatic 2 MiB PDF backups, independent complete cloud readbacks, quiet observation and one BLE lifecycle cycle passed | Longer endurance and a 100-file physical workload |
| Firmware 0.4.3-ui-dev | Installed with independent app/protected-state verification; a new 2 MiB PDF passed automatic backup and full independent cloud hash; native USB health passed | Longer endurance, physical LCD appearance and actual venue acceptance |
| Linux desktop 0.5.3 | Built and installed; both archives and all 3,008 installed regular-file hashes verified; packaged/installed Qt smoke passed; unchanged production diagnostics passed against the real board | Automated packaged-GUI device interaction remains unvalidated |
| Android viewer 0.1.2, code 3 | Built, signed and verified; modern assets rendered at two widths | Installation and acceptance of this exact APK on a phone pending |
| Windows desktop 0.5.3 | Shared interface and Windows build recipe updated | Native Windows build and device acceptance pending; no Windows binary claimed |

Normal backups remain upload-only and run on the dongle. No computer helper,
two-way mirror, deletion propagation or hosted phone page is required.

## Source and artifact identities

Branch: `performance/incremental-and-ui-20261106`.

| Revision | Scope |
| --- | --- |
| `4fc282cb326a1bfb783f059beaa7561d354226f8` | Earlier desktop 0.5.1 / Android 0.1.1 summaries and reduced initial reads |
| `d0bb28ae2e2db328708c8c18684b25cec8d98337` | Incremental proofs, phase reporting and source exporter |
| `cee8a8e64d21917b1640b3a5d782988305623dd8` | Cache-disabled include isolation and aggregate registration; installed 0.4.2 build source |
| `82e5d97282d52da6436a990dcf2d6768110b34e6` | Desktop 0.5.2 explicitly admits reviewed 0.4.2 firmware while rejecting unknown versions |
| `c319dadbbe730a96383219c8e39973419d65b028` | Modern desktop 0.5.3 / Android 0.1.2 / LCD source; firmware 0.4.3-ui-dev candidate |

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Preserved 0.4.2 app only | 1,689,280 | `d0aa666e7fe0e4f47f7bef851bda82415aad10e895779c739eb1bb7ad02191b2` |
| Installed 0.4.3 app only | 1,689,744 | `1b7c24ebd4b9de0750f46f3875c230469533b38ab0fd8234bb14d914361a994e` |
| Reviewed 0.4.2 default BLE/cache-disabled app | 1,396,160 | `fede8a8c5785db39e1f135962f84947f8b60dacc094a71659ad051d3c9f71209` |
| Unchanged partition table | 3,072 | `35897a52e0d951707f219eb6292b945ad55095364e3a8075eeeb4b221e211798` |
| Signed Android 0.1.2 / code 3 | 2,780,915 | `ea15a0336f846f12b062da3ed57f374267fa77d9d826a2e08129344bd700f2b9` |
| Linux 0.5.3 portable archive | 217,746,275 | `59dafeca7fd8f364ea970009fba2bede21a95563dffbe60afb82c8adeb3f8b4c` |
| Linux 0.5.3 Debian package | 217,684,334 | `f9841a00e1c080fa17f75ea8721e22ed6c686a8d615cfeedf8755aeccf380077` |
| c319dad default BLE/cache-disabled app | 1,396,160 | `34451b51d87ed4df6b1f54add081eca80f878dc6daeed64f64625d7ba415fcc2` |

The modern profile `tdongle_s3_product_ui_dev` extends the retained
`tdongle_s3_product_incremental_dev`; default `tdongle_s3`
keeps Bluetooth management and the incremental cache disabled. Guarded firmware
builds check protected SDK inputs, source stability, partitions and USB classes
and buffer sizes. A factory/merged image is not the reviewed app-only artifact.
The 0.5.3 packages use clean, unchanged desktop inputs from `c319dad`. The 0.4.3 UI app
adds 464 bytes against 0.4.2 and retains 84,412 bytes of static RAM.

## Why verified files were checked again

The previous idle proof skipped a whole pass when no USB writes occurred. Adding
a file changed the drive generation, so the next scan read every eligible file's
contents before comparing its receipt. Unchanged verified files were not uploaded
again, but those full local reads still took time.

The optional incremental path reuses only an existing Verified receipt after
checking the same boot and coverage epoch, complete configuration scope, the
canonical 512-byte receipt digest, fresh read-only FatFS layout and size, and
complete observed coverage of physical writes. Coverage is recorded before a
sector changes under the existing storage mutex; callbacks allocate no memory.

New and changed files still hash normally. Same-size edits with preserved
timestamps remain detectable. Missing or corrupt evidence, overflow, changed
layout/configuration, unsupported layout or reboot falls back to full hashing.
The cache never creates Verified receipts, advances their verified time or
replaces cloud upload/readback checks.

A full **local-content audit** becomes due after 24 hours and runs on an eligible
quiet/network/schedule pass. Adding another file cannot postpone that deadline.
This audit is not a fresh download of every cloud copy.

## Speed and memory

The measured upload rate remains approximately **50–54 KiB/s**. Full cloud
readback in the recent trial was approximately 32 KiB/s and is a separate phase.
There is **no newly demonstrated raw upload-speed increase** in this release.
Earlier awake-radio work already contributed to the approximately 24-to-50 KB/s
historical improvement; it is not a new gain from this UI/cache change. Earlier
larger-buffer trials did not demonstrate a useful gain and are not enabled here.

The useful improvement is less repeated work. A native production-core fixture
with **100 verified files plus two later additions** performed zero old-content
hashes, two new-file hashes and zero cache-hit writes. Bounded sidecar reads
totalled 1,997,184 bytes across the two passes; the desktop run took about 31 ms.
These are native fixture results, **not ESP32 timing or physical zero-read proof**.
The pinned FAT16/FAT32 fixture separately checks layout traversal without reading
old content sectors. A physical 100-file workload and content-read counter remain
unvalidated.

| Installed 0.4.2 resource measurement | Result |
| --- | --- |
| Static RAM | 84,412 B; +1,080 B against preserved 0.4.1 |
| Application size | +7,936 B against preserved 0.4.1 |
| Fixed write-coverage object | 1,072 B; 64 bounded ranges |
| Scoped FatFS file object | 4,136 B; checked allocation freed before TLS |
| Disposable LittleFS sidecar | 128 × 192 B = 24,576 B; no SD cache writes |
| Worker stack allocation | Existing 16,384 B retained |
| Startup free internal heap / largest block | 165,676 B / 122,868 B |
| Lowest observed 25 ms sampled free heap / largest block | 36,128 B / 24,564 B; no observed allocation failure |

The SDK's historical heap-low-water value sums per-region minima, potentially
from different times. Its approximately 29 KB result is not a simultaneous free
heap measurement. Sampling also cannot exclude a shorter unobserved dip. Existing
memory, TLS, watchdog and storage protections were not weakened.

## Modern interfaces

Desktop 0.5.3 uses a blue/white layout with a numbered setup rail, large fields
and clear Back/Next controls. Windows uses the same assets. Android 0.1.2 has a
compact login, BOOT guidance, a prominent Verified count, recent saved files,
collapsed secondary details and a clear Done/Close Bluetooth action.

The LCD uses a dark navy palette, compact Wi-Fi/SD indicators, a single progress
bar and explicit IDLE/BUSY/WAIT/OFF/CHECK badges. Finding files, checking local
content, uploading, cloud readback and publishing remain separate. Inactive
retained rates do not appear as live work. Partial and fault states stay explicit.
Existing fonts, redraw cadence and the 88-byte frame are retained.

Optional desktop history is read on demand, without periodic polling. Android
reuses validated initial connection information; Refresh explicitly reads again.
Finish/Done clears private state immediately and waits for native link-close
acknowledgment. A closed app link alone does not prove backups resumed.

Presentation changes leave the Android lifecycle, native bridge and shared
Security 2 modules byte-identical. Its six APK assets match source; the app is
non-debuggable and requests Bluetooth Scan/Connect only, with no Internet
permission. Package identity and signer are unchanged. No private key is embedded.

## Validation completed

| Gate | Scope and result |
| --- | --- |
| Production core | 70/70 checks; source check covers 45 critical-section regions |
| Worker / cancellation | 15 worker cases including 20 restart cycles; 4 cancellation records |
| Incremental / original idle | 25 strict incremental records; 35 cache-disabled idle records |
| Modern LCD | 24 synthetic states; 100,000 model frames and 10,000 actual-renderer frames; zero C++ render allocations |
| LAN dashboard | Native parser/server/passive-close fixtures and 10 actual-script cases |
| Desktop compatibility baseline | 151 Python and 121 frontend cases; installed 0.5.2 hashes and packaged synthetic smoke passed |
| Modern desktop 0.5.3 | 151 Python and 121 frontend cases; nine actual Qt views at 1,120 / 620 / 360 px; clean build, both archives, installed hashes and packaged/installed synthetic smoke passed |
| Android 0.1.2 | 35 JVM, 41 JavaScript and 31 actual-asset Qt visual checks at 390 / 320 px passed; offline release build, lint, signing, alignment, permissions and assets verified |
| Build guard / source exporter | 52 build-guard cases and 8 isolated real-Git export cases passed |

Strict native runs contain no ASan/UBSan/leak diagnostics. Two exact curl-52
messages belong to deliberate mock disconnects. Android retains three reviewed
lint warnings: required JavaScript and two legal-notice resources. Offscreen Qt's
graphics/D-Bus warnings did not bypass its smoke assertions or disable its sandbox.
Qt synthetic rendering is not an Android WebView or real-radio acceptance test.

Failed preparation attempts remain recorded: working-directory/Git mapping errors
before ROM entry, a cache-disabled include dependency, the audit deadline ordering,
and one parallel PlatformIO cache failure. Final firmware builds ran sequentially.
Two Android preview-harness errors concerned Qt return conversion and fixture-only
module resolution; final rendering passed without production JavaScript changes.

## Physical 0.4.2 results and remaining gates

The app-only installation followed safe host removal and worker retirement, two
matching fresh 16 MiB recovery reads, authenticated encrypted recovery reopen and
preimage/layout/security checks. Independent app readback matched the hash above.
Bootloader, configuration and receipt ranges matched their checkpoints before
normal software return. The installer did not write SD payloads or partitions.

Two separately added synthetic **2 MiB PDF files** were automatically detected,
uploaded and fully verified while the dongle remained plugged in and mounted.
Independent complete TLS cloud downloads matched both original SHA-256 hashes.
Pending/failed/error counts were zero. The current three-file inventory includes
one pre-existing file that was not read or changed by the controlled fixture.
Current scan counts and older persisted receipt counts describe different facts.

One setup/BLE lifecycle window stopped and resumed the worker, restored memory
headroom and retained the same boot and USB enumeration. That particular cycle
checked advertisement and lifecycle; it did not authenticate a new GATT session.
A subsequent **405-second observation with 28 samples** retained verified count 3,
zero observed active scans, no host writes and the same boot/USB enumeration.
There is no physical old-content-read counter, so this does not prove zero SD reads.

## Physical 0.4.3 follow-up

The modern app-only update completed after two new matching full 16 MiB reads,
authenticated encrypted reopen and a fresh current-state checkpoint. Independent
written-app readback matched the exact 0.4.3 hash. Bootloader, settings/layout and
receipt ranges matched the fresh checkpoint before controlled software return.
Only the app sectors were written. No SD format, partition/eFuse change or
credential change occurred.

One further exclusive 2 MiB synthetic PDF was then automatically uploaded. An
independent full TLS GET matched its source SHA-256, with certificate/hostname
validation and no redirect or cloud write by the verifier. The final native
snapshot reported revision 7 unchanged, owner ready, local Wi-Fi connected,
worker running/unpaused, four verified/zero pending/failed, and zero worker or
backup error. The new-file observation retained the same USB enumeration and
increasing uptime. No physical old-content-read counter is available.

The 25 ms sampler's minima after that transfer were 38,860 B free internal heap and
26,612 B largest block; no allocation failure was observed. Final free heap was
165,564 B, largest block 102,388 B, worker stack free 7,748 B. Historical per-region
heap watermarks remain distinct from these sampled measurements.

The installed-GUI automation stopped at its Qt/Playwright debug connection before
USB contact and closed cleanly. It was not counted as a hardware pass. The
fallback exercised the unchanged production diagnostic channel and existing
firmware whitelist against the real board. Its first summary incorrectly treated
Wi-Fi result 20 as a fault; source and existing regressions establish that 20 is
the intentional LocalOnly association/DHCP result. Only that private test
expectation was corrected, with seven offline checks including rejection of
WrongPassword (14). Firmware, authentication and memory protections were unchanged.
Prior failed results remain preserved.

This exact Android APK's phone test, native Windows acceptance, packaged-GUI
interaction, multi-day endurance, a 100-file physical workload and actual
workplace/portal tests remain separate gates. Card hot swapping remains
unsupported without a proper restart because there is no independent card-detect pin.

## Delivery and publication

The public [GitHub overview](https://github.com/rtariq2015/cloud-stick-pro) contains
attribution and synthetic previews; source and binaries have not been published.
The modern README and all three synthetic renderer images were published at
`f9f04a9e24de35fe1f708d733188222ea756b22d` and fetched back byte-for-byte. This
published snapshot recorded the then-running firmware installation. Final public
notes and overview at `5e86b36ac86c2de6626f22b2e5307cece5c21ccd` now record its
actual result and were also fetched back byte-for-byte. No application/firmware
source was uploaded.

The committed-source exporter assembles one `cloud-stick-pro/` folder using a
reviewed public-document allowlist and per-file SHA-256 manifest. Private recovery
data, signing material, credentials, raw operator dossiers and Git history stay
out. ELF/map debug products remain developer-local. Original component notices
and historical work are preserved. See the [ordered public history](../PROJECT_HISTORY.md).

The independent project is `~/Projects/cloud-stick-pro`, with its own
`cloud-stick-pro.code-workspace` and HTTPS origin pointing to the owner's GitHub
repository. It contains firmware, both app sources, tests, tools and guides.
Local installers/images/evidence live under its Git-ignored
`releases/2026-11-06-current/`. Original worktrees and private recovery data stay
preserved separately. See the [workspace guide](../CANONICAL_PROJECT.md).

Original **John / JMcrafter26** attribution remains prominent. The upstream README
declares MIT but its linked grant text is absent; [license clarification](../CREDITS.md)
still gates source/binary redistribution. A local review package does not resolve it.
