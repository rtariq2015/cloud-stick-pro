# Release notes — 7 November 2026

## Cache-stress Bluetooth cleanup and Wi-Fi round trip passed

The private **0.4.6-cache-lab** image passed real SDK settings-cache stress and
repeated Bluetooth closure. Cache cleanup restored the largest block from
**69,620** to **139,252 bytes** without changing any NVS flash byte; the unchanged
memory proof passed and backups resumed. Two controlled local driver drops then
connected to a different saved Wi-Fi profile and back, with fresh backup workers,
five verified files and zero pending/failed or backup errors. Both access points
remained available; this was not an actual access-point outage test.

Normal **0.4.6-cache-dev** is installed and passed two more Bluetooth-close
cycles with Wi-Fi/backups resuming, five verified files and no reported errors.
Protected state and an encrypted-checkpoint logical settings comparison passed.
An early startup-status precondition failure cleared before the strict trial;
the report records that limitation. The lab binary is never a download, and the
owner's exact authenticated phone Save/Finish flow still needs acceptance. Desktop **0.5.7** is installed: **Diagnose my stick**
now explains cleanup failures and paused backups with a next step. Native Linux
packaging/startup and 127 frontend / 151 Python tests passed. Android stays
**0.1.2**. Source and artifact hashes are in the
[acceptance report](releases/2026-11-06-ble-cleanup.md).

## Authenticated test found the timing correction insufficient

The 0.4.5 image again showed “BLE needs review” after the owner saved Wi-Fi and
closed Bluetooth. Revision **9** was durable, but the largest block remained
**61,428 bytes** at the **30,001 ms** deadline. Five verified files and zero
backup failures were retained. The device was returned to normal operation with
a separately checked read-only ROM diagnosis, without writing flash/settings.

Candidate **0.4.6-cache-dev** adds a checked NVS RAM-cache lifecycle after
verified shutdown; the entire saved partition must remain byte-for-byte equal
and the original memory proofs still apply. Its guarded normal and private
service builds passed. The subsequent service-image acceptance is recorded
above; normal-image acceptance passed, while personal authenticated-flow acceptance
remains pending.
Desktop **0.5.6** initially added exact compatibility; **0.5.7** now adds guided
diagnostics. Android is unchanged. The private cache/failover fixture image is
not a release binary. See the
[investigation report](releases/2026-11-06-ble-cleanup.md).

## Earlier timing-only candidate (0.4.5)

After a real Wi-Fi save on `0.4.4-wifi-dev`, the saved revision advanced but the
dongle showed “BLE needs review” and paused backups. The verified stop/release
path sampled its contiguous-memory proof at 2,001 ms; a later read-only health
check showed that memory had fully recovered. Five previously verified files
and zero backup failures remained recorded. This is tracked in
[issue #5](https://github.com/rtariq2015/cloud-stick-pro/issues/5).

Candidate **0.4.5-ble-dev** allows delayed SDK destruction to settle within the
existing 30-second closing deadline. Wi-Fi and backups still wait for verified
shutdown and the original memory checks. A failed stop or expired deadline
remains blocked. USB service continues during the wait; settings are not
rewritten. Desktop **0.5.5** adds exact-version compatibility. Android **0.1.2**
is unchanged. The protected app-only installation and native health passed,
with revision 8, five verified files and zero backup errors. The subsequent
authenticated save/close trial failed as recorded above. See the
[correction report](releases/2026-11-06-ble-cleanup.md).

Local verification passed: 64 strict owner-loop build/run records with address,
undefined-behavior and leak sanitizers (simulated hardware), including delayed
recovery, the unchanged deadline, clock rollover and stop/release/reinit failures;
54 guarded-build tests; and 151 desktop Python / 121 interface tests. The guarded
SDK build passed with unchanged partitions and reviewed USB/storage sources.
Its initial app image is 1,689,840 bytes, SHA-256
`d9579044bf9df00e2a80149f89162a486dfbef7ba30cf8ea5f01c719ee94734c`.
Installation alone does not establish authenticated setup-close acceptance.

## Installed Wi-Fi fallback update

Firmware **0.4.4-wifi-dev** improves the existing saved-network fallback so slow
connection failures do not repeatedly favor preferred networks over untried
entries. Native policy and real owner-loop simulations pass. The guarded
build and app-only installation passed, including two fresh matching encrypted
recovery reads, independent app/protected-state readback and native health.
Setup-close Wi-Fi reconnection and a new 2 MiB PDF/independent full cloud hash
passed on the board. Final status reported five verified and zero pending/failed
or backup errors. Linux **0.5.4** is installed
and recognizes this exact firmware version. Real switching between two distinct
Wi-Fi networks remains unvalidated. Android **0.1.2** is unchanged; previous
release artifacts are preserved. See the
[candidate report](releases/2026-11-06-wifi-fallback.md).

## Preserved preview-2026.11.06 downloads

[preview-2026.11.06](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.06)
provides the existing verified Linux setup **0.5.3** `.deb` and alternative
`.tar.gz`, signed Android viewer **0.1.2** APK, and firmware **0.4.3-ui-dev**
app-only image. **INSTALL.md**, **SHA256SUMS** and **release-manifest.json**
describe compatibility, exact binary identities and installation boundaries.
See [Updates](UPDATING.md) to choose the right component.

Linux targets Ubuntu 24.04 x86-64 / glibc 2.39 or a compatible system. Android
requires version 12 or later. No native Windows executable is included. The
firmware image is for a reviewed existing compatible Pro layout on the original
T-Dongle-S3; it is not a stock-board/factory installation package.

The binaries are unchanged from the recorded local candidates. Publication does
not add acceptance for packaged-GUI hardware interaction, the exact APK on a
phone, native Windows, actual venue portals, physical LCD appearance or multi-day
endurance. Those limits and the unresolved upstream license-file evidence remain
explicit. Earlier entries below retain their original publication state.

## Complete development source upload

Published the reviewed firmware, desktop Linux/Windows app, Android viewer,
tests, guides and retained component notices in one source tree. Private keys,
recovery images, build caches and user data are excluded. The upstream README's
MIT assertion and missing license-file evidence remain explicit; no substitute
license is supplied. Earlier entries below describe their original publication
state. Packaged binary downloads and hardware acceptance remain separate.

GitHub checks now cover the exact source file set plus desktop and Android
interface tests. Firmware and companion application code/binaries are unchanged
by this upload; the BlueZ mock now uses a synthetic device address.

## GitHub maintenance setup

Added a plain-language update guide, structured bug/feature forms, a pull-request
template, read-only repository checks, weekly official-upstream monitoring and
Dependabot proposals for the pinned GitHub Action. Changes and failures are
reviewed before release; no automatic merge, signing, deployment, firmware flash
or cloud-account access is configured. These checks cover the public preview,
not the held product source or physical hardware. See [Updates](UPDATING.md) and
[Maintenance](MAINTENANCE.md).

Firmware and companion binaries are unchanged in this maintenance update.
Derived source/installers remain local pending the existing upstream license-text
clarification. The public repository is not yet an installable product release.

Cloud Stick Pro now has a dedicated source project and VS Code workspace, with firmware, Linux/Windows app sources, Android app source, tests and guides together. Its Git origin is this repository. The source remains local while the documented upstream license grant text is clarified; this public repository contains the overview, notes and synthetic previews.

## Earlier component round (0.4.3 / 0.5.3)

| Component | Result |
| --- | --- |
| Firmware 0.4.3-ui-dev | Installed on the development board; exact app and protected ranges independently verified after two fresh matching encrypted/reopened 16 MiB recovery reads |
| Linux setup 0.5.3 | Installed; 151 Python and 121 frontend cases, nine Qt layout views, both archives and all 3,008 installed regular-file hashes verified |
| Android viewer 0.1.2 | Signed APK; 35 JVM, 41 JavaScript and 31 synthetic visual checks passed; same signing identity; no Internet permission |
| Windows setup 0.5.3 | Shared modern UI and build recipe updated; no native Windows binary or acceptance claimed |

Application/firmware code revision: `c319dadbbe730a96383219c8e39973419d65b028`.
Installed firmware app: 1,689,744 bytes at app address `0x10000`; SHA-256 `1b7c24ebd4b9de0750f46f3875c230469533b38ab0fd8234bb14d914361a994e`.

## Backup and resource checks

Two synthetic 2 MiB PDFs passed automatic upload and independent complete cloud hashes on 0.4.2. A further new 2 MiB PDF passed the same checks after 0.4.3 installation. Final production-source USB diagnostics reported four verified, zero pending/failed/backup errors, unchanged configuration revision, connected local Wi-Fi and no observed allocation failure. No account setting, partition, eFuse or existing local file was changed by the app-only installer.

The new content-proof cache avoids re-reading unchanged verified contents when bounded write-coverage, layout and receipt evidence remains valid. A 100-old-plus-two-new-file native fixture performed zero old-content hashes. This is not a physical 100-file timing measurement. Reboots, uncertainty and the daily local audit can still require full checks.

Measured raw upload rate remains around 50–54 KiB/s; no new raw-rate increase is claimed. Earlier awake-radio work already improved approximately 24 KB/s to 50 KB/s. The modern LCD adds 464 app bytes and no static RAM against 0.4.2, keeps its 88-byte frame and performs no C++ render allocations in the tested renderer.

## Remaining acceptance work

The exact new Android APK, native Windows, actual workplace captive portals, physical LCD appearance and multi-day endurance still need their own acceptance. Packaged-GUI automation stopped at its Qt debug connection before USB contact; it was not counted as a pass. The unchanged production native diagnostic code was tested successfully against the real board. A private test's LocalOnly20 expectation was corrected without changing firmware.

The target remains the original LILYGO T-Dongle-S3 with 16 MiB flash and no PSRAM. The queue remains bounded to 128 path identities. This is upload-only backup; it does not mirror deletions or download cloud changes. Generic new-board installation and universal captive-portal support are not completed features.

Original project: [John / JMcrafter26 — Cloud Stick](https://github.com/JMcrafter26/cloud-stick). Hardware references: [LILYGO](https://github.com/Xinyuan-LilyGO/T-Dongle-S3). Existing component notices remain preserved. No replacement upstream license grant has been invented.
