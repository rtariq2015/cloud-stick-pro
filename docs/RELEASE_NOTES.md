# Release notes — 6 November 2026

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

## Current component round

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
