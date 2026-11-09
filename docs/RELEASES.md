# Releases and revision records

Firmware, desktop and Android have separate version numbers. Setup 0.7.3 can
install firmware when you explicitly choose and confirm that operation. Installing
the computer app itself updates the computer program.

| Component | Preserved / installed evidence | Current published preview |
| --- | --- | --- |
| Firmware | Earlier recovery images preserved | 0.4.8-discovery-dev; protected existing-stick install and phone connection/cleanup checks pass; first unconfigured board and access-point outage remain pending |
| Linux desktop | User 0.7.2 installed; prior programs retained | 0.7.3; native package/installer checks pass. Actual 0.7.2 authenticated desktop history/Done also passed; new-version install round trip separate |
| Android viewer | 0.4.3-dev/code 10 installed, pulled hash matched | 0.4.3-dev/code 10; software/build checks pass. Fresh phone UI test stopped at keyguard before authentication; earlier 0.4.2 has 30 phone assertions |
| Windows desktop | Native 0.7.3 build | ZIP/EXE, protection APIs, installed-program checks and install/uninstall pass in CI; physical USB/Bluetooth remains pending |

## Guided companion preview — 8 November 2026

[Download Setup 0.7.3 and Viewer 0.4.3](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-guided-companion).
All nine public asset URLs, sizes and SHA-256 digests passed fresh unauthenticated
download checks. App catalog revision 2 is signed; installation stays explicit.
The exact native/Android source is `369e2ef21b8053812e449a6bc5549be357b77ba8`.
The separately frozen firmware source is `f25d8a5e30df7be7dafd5a5fe2ef736a5c8ac11b`.
[Versions, asset hashes and test limits](releases/2026-11-08-companion-components.json) ·
[Full version report](releases/2026-11-08-guided-companion.md).

Older entries below describe their own versions and download state at that time.

The new source work starts from combined suite commit
`d4ab3bb7bcb16f56f385ff275f36fe27dc6773a9`. The original project remains credited
to [John / JMcrafter26](https://github.com/JMcrafter26/cloud-stick), baseline
`27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d`.

## Trusted-phone candidate — 8 November 2026

Firmware **0.4.7-wake-dev** and Viewer **0.3.0-dev/code 6** are installed on the
owner’s configured board and phone. All 27 physical-phone checks passed, including
first trust, two signed Wi-Fi requests opening Bluetooth without BOOT, normal
personal authentication, private history, Refresh and clean return to backups.
Three independent close cycles restored memory/Wi-Fi/backups with five verified
records and no reset. An invalid proof was denied without pausing backups.

These are limited candidate tests, not a claim that the older intermittent memory
fault is fixed. Public downloads remain the table above pending separate package
publication. [Instructions](BUTTON_FREE_CONNECTION.md) ·
[Version report](releases/2026-11-08-button-free-development.md) ·
[Sanitized physical record](releases/2026-11-08-button-free-physical.json).

## Desktop ROM recovery correction — 8 November 2026

Setup **0.6.1** source corrects the 16 MiB ROM geometry before full recovery
reads. The earlier attempt failed safely before any firmware write. Read-only
physical boundary checks pass; complete recovery/install and native packaging
acceptance are recorded separately in [the version report](releases/2026-11-08-desktop-rom-geometry.md).
The offered 0.6.0 download is unchanged until a new package is verified/published.

## Android emulator-tested preview — 8 November 2026

[Viewer 0.2.1](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-android-tested)
fixes delayed invalid-address errors and initial connection guidance. The
[emulator/physical-stick report](releases/2026-11-08-android-emulator.md) separates
actual Android emulator tests, real stick LAN access, virtual Bluetooth scanning,
real Zorin Bluetooth metadata/cleanup and real Pixel authenticated history. The
later phone follow-ups separately record a passing round trip and a remaining
firmware memory-recovery failure.
Firmware remains 0.4.6-cache-dev; desktop remains 0.6.0.

## Android Wi-Fi dashboard — 8 November 2026

[Viewer 0.2.0](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-android-dashboard)
adds a live Wi-Fi home screen and remembers the stick’s address. Private history
keeps its Bluetooth sign-in. No firmware or desktop update is needed. Native
software, UI and synthetic rendering checks are recorded separately from pending
physical phone acceptance in the [version report](releases/2026-11-08-android-dashboard.md).

## All-in-one desktop preview — 7 November 2026

[Setup 0.6.0 for Windows and Linux](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.07-integrated-setup)
adds the signed firmware installer and handoff into login/Wi-Fi/cloud setup.
Firmware is unchanged 0.4.6-cache-dev; Android is unchanged 0.1.2. Native software
and Windows per-user installation checks pass. First installation on a new board
and physical Windows USB/Bluetooth work remain pending.

The [0.6.0 report](releases/2026-11-07-integrated-setup.md) separates actual native
software checks, fake-ROM tests and prior hardware results. Release manifests
record exact package-source commits, sizes and hashes. The separate
[firmware-data release](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/firmware-data-0.4.6-r1)
is a builder input, rather than the computer app most users need.

## Earlier Bluetooth preview — 7 November 2026

[Download the Bluetooth cleanup and diagnostics preview](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.07-ble-cache).
It contains normal firmware **0.4.6-cache-dev**, Linux Setup **0.5.7**, source and
hashed installation/validation records. Android **0.1.2** stays in the preceding
`preview-2026.11.06` release.

The normal image passed protected installation and two Bluetooth closes with
Wi-Fi/backups resuming, five verified files and zero pending/failed or backup
errors. A private service image separately passed real NVS cache stress and a
controlled round trip between two distinct saved Wi-Fi networks. That trial
kept both access points available; it was not an AP power-off test. The exact
personal authenticated phone Save/Finish flow remains unverified. The lab binary
is not a download. See the [exact source, hashes and acceptance report](releases/2026-11-06-ble-cleanup.md).

**Diagnose my stick** explains reported cleanup failures, paused backups and
other local states. The [desktop screenshots](../README.md#a-look-at-the-interfaces)
use synthetic example data, not hardware evidence.

## Earlier improvement round — 6 November 2026

The focused change is to avoid re-reading every verified file's contents when a
different file is added. This requires bounded evidence of which physical USB
sectors changed, a fresh file-layout check and an exact existing verified receipt.
File names, lengths and timestamps alone do not establish that a file is unchanged.
Missing evidence always selects the original full-content check.

The modern desktop and Android apps use a blue/white layout. Desktop has a
numbered setup rail; Android prioritizes recorded counts and recent files. The
modern LCD uses a navy palette, compact indicators and explicit activity badges.
It and the local dashboard distinguish finding files, checking local content,
uploading and cloud readback. Retained transfer fields do not become ongoing work
once an operation is inactive. Partial results remain partial.

Desktop and Android summaries distinguish a new local status read from a
previously recorded backup result. The apps avoid redundant initial reads and
defer optional records until the user opens them. Finishing waits for the device
acknowledgment; a close acknowledgment alone is not proof of a new cloud upload.

Installed 0.4.2 automatically backed up two separate 2 MiB synthetic PDFs;
independent full cloud downloads matched both original SHA-256 hashes. One BLE
lifecycle cycle and a 405-second quiet observation passed on the same boot/USB
enumeration. The native 100-file fixture skipped old-content hashing; a physical
100-file workload and old-content-read counter remain unvalidated. Raw upload
rate remains approximately 50–54 KiB/s; no new raw-speed improvement is claimed.

Exact build hashes, resource comparisons, tests and remaining acceptance gates
are in the version-specific report. A screenshot, source test or successful
compilation must not be labelled physical acceptance of a new image.

Evidence for that earlier round: [2026-11-06 improvement report](releases/2026-11-06-improvements.md).

## Ordered history

* [All-in-one Setup 0.6.0](releases/2026-11-07-integrated-setup.md): native Windows/Linux installer, verified public data, private recovery and clear first-account handoff; hardware limits recorded separately.

* [Bluetooth cleanup and diagnostics update](releases/2026-11-06-ble-cleanup.md):
  firmware `0.4.6-cache-dev`, Linux Setup `0.5.7`, protected normal installation,
  repeated closure, cache stress and controlled two-network switching; personal
  authenticated phone Save/Finish remains unverified.

* [Saved Wi-Fi fallback update](releases/2026-11-06-wifi-fallback.md):
  `0.4.4-wifi-dev` and desktop `0.5.4`, guarded build, protected app-only update,
  native health and setup-close Wi-Fi recovery passed. Distinct-network
  switching was unvalidated on that image; the later 0.4.6 service trial is
  recorded separately above.

* [Changelog](../CHANGELOG.md): user-visible changes, newest first.
* [Public project history](PROJECT_HISTORY.md): ordered, sanitized component milestones and exact source identities.
* Raw operator logs, recovery/enrollment dossiers and machine history remain private and unchanged.
* [Project layout](PROJECT_LAYOUT.md): where current source and supporting material live.

Historical reports describe their original image and test scope. For example,
an early BLE-only image intentionally disabled backups; that is not the behavior
of the later combined firmware. Do not apply an old image's test verdict to a
new candidate merely because its version number is similar.

## Package layout

The source exporter assembles one `cloud-stick-pro/` folder. Firmware source stays
at the root, desktop code stays in `desktop/`, and Android stays in `android/`.
Documentation uses an explicit reviewed allowlist: current public guides,
component notices, named synthetic previews and the curated history index.
Raw operator/recovery/handoff/history dossiers, the original working README
and raw machine-history JSON are excluded rather than automatically moved.
The exporter reads committed Git blobs, not the live working directory, and emits
`SOURCE_MANIFEST.json` with an SHA-256 for every exported file. Git history, local
device data, build caches, signing material and recovery vaults are excluded.
Uncommitted documentation is not captured: explicitly review and identify a
documentation revision or separately manifested overlay before packaging it.
Debug ELF/map products remain developer-local, outside the public source
package; app images and pinned binary identities are not changed by this curation.
The reviewed source snapshot is published with its manifest and retained license
evidence. Binary releases have separate packaging, signing and acceptance gates.

```sh
python3 tools/export_public_project.py --revision HEAD \
  --output ../cloud-stick-pro \
  --archive ../cloud-stick-pro-source.zip
```

This command creates a local review package only. It performs no upload, device
operation, install, erase or format. It refuses existing output rather than
replacing it. Publication is a separate action; preserve the unresolved upstream
license evidence described in [Credits](CREDITS.md).
