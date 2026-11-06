# Cloud Stick Pro

**Keep the USB plugged in. Copy files into `QA`. Check what was backed up.**

Cloud Stick Pro is a USB-to-WebDAV backup project for the original **LILYGO
T-Dongle-S3**. The dongle performs backups itself. A Linux setup application and
an Android nearby viewer provide local setup, troubleshooting and recorded status.

Based on **[Cloud Stick by John / JMcrafter26](https://github.com/JMcrafter26/cloud-stick)**.
His original project is the starting point for this work; this is a derivative
project, not an official release from JMcrafter26 or LILYGO.

> **Development preview.** This repository currently presents the project and
> interface previews. Firmware/application source and binary releases are not
> published here yet. Candidate validation and upstream license clarification
> remain open; there is no downloadable installer on this page.

## How it works

1. Configure a supported Pro dongle using the desktop wizard and your own login.
2. Choose an authorized Wi-Fi network and an HTTPS WebDAV destination, such as Koofr.
3. Copy files into the selected folder, normally `QA`, and keep the dongle powered.
4. After USB activity settles, the dongle uploads new or changed files and checks
   the cloud copy using full-content SHA-256 readback.
5. Review **Verified** receipts on the LCD, local dashboard or nearby Android viewer.

The computer remains the writer to the microSD filesystem. New host writes cancel
an incompatible read session. This project performs **upload-only backup**:
it does not mirror deletions, download remote changes or erase local originals.

## Local applications

| Interface | Purpose |
| --- | --- |
| Linux setup application | Step-by-step USB selection, personal login, Wi-Fi search, cloud setup and explicit Save/Finish; on-demand diagnostics |
| Android viewer | Nearby Bluetooth sign-in and recorded backup results, including up to eight saved file summaries; no hosted website or Internet permission |
| Dongle LCD | Clear phases for finding files, checking local content, uploading and cloud verification, plus waiting/error states |
| Local dashboard | Read-only status on the dongle's current network address |

The desktop firmware service currently reviews manifests; a general firmware
installer/updater is not enabled. Windows source/build recipes exist, but an
accepted Windows binary is not claimed.

## Interface previews

These images use synthetic data. They demonstrate the actual interface renderers,
not completed hardware acceptance or a live account.

### Desktop

![Modern desktop setup wizard preview](docs/images/desktop-setup.png)

### Android

<img src="docs/images/android-viewer.png" alt="Android recorded-backup viewer preview" width="400">

### LCD

![Synthetic LCD states using the existing display fonts](docs/images/lcd-states.png)

## Current improvement work

* Avoid re-reading verified file contents when a different file is added, using
  bounded USB-sector change evidence and fresh file-layout/receipt checks.
* Keep same-size edits detectable. File names, sizes and timestamps alone do not
  prove content is unchanged. Missing evidence falls back to full checking.
* Avoid redundant Bluetooth reads and load optional history only when requested.
* Make current activity, saved results and incomplete scans visibly different.
* Modernize the desktop wizard, Android recorded-results view and LCD together.
* Keep source, tests, guides, ordered revision notes and release manifests together.

Firmware `0.4.2-incremental-dev` is installed on the development board. Two
automatic 2 MiB synthetic PDF backups passed independent full cloud readback.
The modern LCD firmware `0.4.3-ui-dev` is installed and passed a new automatic
2 MiB PDF backup with independent full cloud hash and native USB health checks.
Modern desktop `0.5.3` is installed locally and integrity/smoke verified;
Android `0.1.2` is built and signed,
with exact-version phone acceptance pending. Windows `0.5.3` uses the shared
interface/build recipe; a native Windows binary is not claimed. App installation
does not update the dongle.

Development is organized as one independent **Cloud Stick Pro** VS Code project,
with firmware, desktop, Android, tests and guides together. Local installers and
review artifacts stay in a Git-ignored releases folder; private recovery material
remains outside the source project. See [release notes](docs/RELEASE_NOTES.md).

Measured upload rate is still approximately **50–54 KiB/s**. The incremental
change reduces repeated local checking; it is not a new raw upload-speed gain.

## Supported scope and limits

The target is the original ESP32-S3 board with **16 MB flash, microSD, a 160×80
display and no PSRAM**. A generic USB drive or a similarly named C5/S3 Plus is
not an interchangeable target.

The queue is bounded to 128 path identities; the Android view shows up to eight
summaries. A verified receipt describes a recorded file version, not unsaved
computer memory or a guarantee against later cloud deletion. Bluetooth setup
pauses conflicting backup work. Guest-network/captive-portal assistance remains
experimental and needs testing on the actual permitted network. A new-board
factory installer, universal portal support and unattended firmware updates are
not completed features.

## Credits and license status

Original project: **[John / JMcrafter26 — Cloud Stick](https://github.com/JMcrafter26/cloud-stick)**,
baseline [`27eac0d`](https://github.com/JMcrafter26/cloud-stick/tree/27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d).
Hardware and reference examples: **[LILYGO T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3)**.

Thanks also to the Espressif, NimBLE, TinyUSB, TFT_eSPI, ArduinoJson, TinyXML-2,
Qt/PySide6, Bleak and AndroidX maintainers. Inherited display artwork retains
upstream's sbrin/Lopaka credit. Component notices will accompany their source
and binary packages.

The original README declares MIT and links a `LICENSE` file, but that file is
absent from the official repository at the reviewed commit. No replacement
upstream copyright notice or blanket project license has been invented here.
Source/binary redistribution remains pending clarification of that missing text.
