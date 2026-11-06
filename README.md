# Cloud Stick Pro

**Keep the USB plugged in. Copy files into `QA`. Let the dongle back them up.**

Cloud Stick Pro is upload-only USB-to-WebDAV backup firmware for the **original
LILYGO T-Dongle S3**: ESP32-S3, 16 MB flash, microSD and a 160 × 80 display,
without PSRAM. A Linux desktop application guides setup, and a local Android
viewer shows nearby backup status using your personal login. Normal backups run
on the dongle without a computer helper or hosted setup website.

Derived from **[Cloud Stick by John / JMcrafter26](https://github.com/JMcrafter26/cloud-stick)**.
His original work and acknowledgements are preserved. This project adds a
different backup/storage safety model, verified receipts and native companions.

**Development preview:** the complete reviewed firmware, desktop, Android,
tests and guides are published here, with downloads in the prerelease below.
This is not a completed general release or a universal installer.
The original README asserts MIT; its linked license file
is missing. That unresolved evidence is retained in [Credits](docs/CREDITS.md#upstream-license-evidence),
without inventing a blanket license or removing upstream notices.

[Get started](docs/GETTING_STARTED.md) · [Developer guide](docs/DEVELOPER_GUIDE.md) ·
[Project map](docs/PROJECT_LAYOUT.md) · [Revision records](docs/PROJECT_HISTORY.md) ·
[Security](SECURITY.md) · [Credits](docs/CREDITS.md)

For future releases, follow [Updates](docs/UPDATING.md) and the
[maintenance policy](docs/MAINTENANCE.md). Bug reports can describe what you
clicked and what happened; programming knowledge is not required. The public
repository checks the reviewed public file set and companion interface logic.
These checks are separate from firmware builds and physical device acceptance.

## Download the preview

Open [preview-2026.11.06](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.06)
and choose the component you need:

| Download | Supported use |
| --- | --- |
| [Linux setup 0.5.3 `.deb`](https://github.com/rtariq2015/cloud-stick-pro/releases/download/preview-2026.11.06/cloud-stick_0.5.3_amd64.deb) | Ubuntu 24.04 x86-64 / glibc 2.39, or a compatible system |
| [Linux setup 0.5.3 `.tar.gz`](https://github.com/rtariq2015/cloud-stick-pro/releases/download/preview-2026.11.06/cloud-stick-0.5.3-linux-x86_64.tar.gz) | Alternative user-local package for the same Linux baseline; choose one Linux package |
| [Android viewer 0.1.2 APK](https://github.com/rtariq2015/cloud-stick-pro/releases/download/preview-2026.11.06/Cloud-Stick-Viewer-0.1.2.apk) | Android 12 or later; exact-version phone acceptance remains pending |
| [Firmware 0.4.3 app image](https://github.com/rtariq2015/cloud-stick-pro/releases/download/preview-2026.11.06/cloud-stick-pro-0.4.3-ui-dev-app.bin) | App-only update for a reviewed existing compatible Pro layout; not a stock-board/factory installer |

Read the release's **INSTALL.md** and compare downloads with **SHA256SUMS** or
**release-manifest.json**. Installing a desktop or phone app does not update the
dongle. Windows has source/build recipes, but no native Windows download yet.
See the [update guide](docs/UPDATING.md) for the steps and remaining test limits.

## What it does

- **Automatic backups:** watches the configured folder, waits for USB activity to
  settle, and uploads eligible new or changed files. An optional saved schedule
  limits when backup passes start.
- **Verified results:** streams file data, checks cloud content with SHA-256
  readback and records verified receipts separately from upload completion.
- **Keep working locally:** the computer remains the writer to the microSD.
  Host writes invalidate the dongle's read session so work can retry safely.
- **Guided desktop setup:** separate Next/Back steps for USB selection, protected
  login, Wi-Fi search, cloud settings and Finish. Changes need explicit save.
- **Useful status:** LCD phases, progress and rates; a read-only local dashboard;
  bounded authenticated history and explicit desktop health refresh.
- **Nearby Android viewing:** a bundled, offline interface for recorded backup
  status and up to eight file summaries. No hosted website or Internet permission.

There is **no two-way mirror, cloud-to-USB download or automatic local deletion**.
Receipts track the file version that was verified; they are not a live guarantee
against later cloud deletion or files still held in the computer's memory.

## A look at the interfaces

These are **synthetic interface previews**, not photographs or evidence that a
new candidate passed physical acceptance. They contain no real account or file data.

### Desktop setup

![Cloud Stick desktop setup preview](docs/images/desktop-setup.png)

### Android viewer

![Cloud Stick Android viewer preview](docs/images/android-viewer.png)

### Dongle display

![Cloud Stick LCD state previews](docs/images/lcd-states.png)

Finding files, reading a local file, uploading, verifying the cloud copy and
publishing are distinct phases. Waiting, paused and recorded results should stay
visually distinct from current work.

## Start here

You need the original supported board, a suitable microSD, an authorized Wi-Fi
network and an HTTPS WebDAV account such as Koofr. A generic USB flash drive,
T-Dongle C5 or S3 Plus is not an interchangeable firmware target.

1. **Check the device's firmware first.** An already configured Pro dongle can use
   the desktop setup application. Installing Pro on a stock board is a separate,
   guarded provisioning process; the app is not a general factory flasher.
2. **Open Cloud Stick Setup on Linux.** Find and check the correct USB, then follow
   the wizard. Use the existing login for an owned stick. Creating a first owner
   on reviewed firmware requires an actually Missing owner and its physical gates.
3. **Choose Wi-Fi and cloud settings.** Search nearby networks, check the connection
   and explicitly save intended changes. Use a dedicated cloud app password.
4. **Finish setup and copy a small test file into `QA`.** Leave power connected and
   allow the saved quiet period or schedule to run.
5. **Look for Verified.** Uploading 100% comes before cloud readback and the durable
   receipt. Review the recorded result and keep your local original.

The full [getting-started guide](docs/GETTING_STARTED.md) explains each step,
the [desktop guide](desktop/README.md) covers Linux packages and source launch,
and the [Android guide](android/README.md) explains the viewer.

## Versions and evidence

| Component | Installed / preserved evidence | Current revision |
| --- | --- | --- |
| Dongle firmware | `0.4.1/0.4.2` recovery apps preserved; earlier two-PDF physical trial retained | `0.4.3-ui-dev` installed; new automatic PDF/full cloud hash and native health passed |
| Linux setup | Previous installations preserved | `0.5.3` installed; archive/installed hashes and smoke verified; production source-native USB checks passed; packaged GUI interaction separate |
| Android viewer | Earlier APKs preserved; phone baseline separate | `0.1.2`, code 3: modern recorded-results view, signed and verified; exact-version phone acceptance pending |
| Windows setup | Shared desktop source/build recipe | `0.5.3` recipe updated; native Windows binary/acceptance pending |

These versions belong to different components. An app installation does not
update the dongle. The desktop firmware service currently **reviews manifests
only**; a generic update writer is not enabled. Windows build recipes exist,
but no accepted Windows binary or native Windows device workflow is claimed.

Earlier builds have bounded current-board USB, Wi-Fi, cloud upload/readback and
recovery evidence. Those results do not certify a new image. Use the
[progress/release index](docs/PROJECT_HISTORY.md),
[ordered revisions](docs/PROJECT_HISTORY.md#ordered-milestones) and exact release manifests to find
which source, image and tests a claim applies to.

## Build and contribute

Firmware profiles and dependency pins are in `platformio.ini`; the default
`tdongle_s3` build remains BLE-disabled. For a source build without uploading:

```sh
pio run -e tdongle_s3
```

Product candidates use the separate guarded profile/build checks described in
the [developer guide](docs/DEVELOPER_GUIDE.md). Desktop dependencies are pinned in
`desktop/requirements.txt`; Android has its own pinned Gradle toolchain.
Read [Contributing](CONTRIBUTING.md) before changing storage, authentication,
SDK patches or persistent formats. Source tests and synthetic render previews
are different from hardware acceptance.

## Practical limits

- The original board has limited RAM and no PSRAM. SD capacity does not add heap.
  Recent upload measurements are approximately 50–54 KiB/s. The new cache reduces
  repeated checking; no new raw-speed gain or MB/s promise is made.
- The persistent queue tracks **128 path identities**. Folder depth, path length,
  directory enumeration and configured file-size limits also apply. The default
  file-size cap is 64 MiB. The Android view shows at most eight summaries.
- A quiet USB does not prove an application has closed/flushed its file. New
  writes can postpone a pass; saved changes are checked later. Power must remain
  available for the dongle to work.
- Bluetooth setup pauses conflicting backup work. Finish or disconnect through
  the interface to return to normal operation; unplugging is a different action.
- Open guest Wi-Fi and captive-portal assistance are experimental and venue
  dependent. Terms may expire or need accepting again. A phone's Internet access
  does not prove the dongle itself can reach its cloud destination.
- Credentials in internal flash and files on removable media are not protected
  against physical extraction by a completed secure-boot/encryption deployment.
  Clinical/workstation deployment, new-board provisioning and long endurance
  require their own acceptance.

The [storage guide](docs/STORAGE_SAFETY.md),
[security policy](SECURITY.md) and
[release evidence](docs/PROJECT_HISTORY.md) explain these boundaries.
Raw working reports remain preserved separately. The
[public milestone index](docs/PROJECT_HISTORY.md) records sanitized source identities and scope.

## Acknowledgements

Thank you to **John / JMcrafter26** for the original Cloud Stick, **LILYGO** for
the board and examples, and the Espressif, TinyUSB, TFT_eSPI, ArduinoJson,
TinyXML-2, Qt, Bleak and AndroidX maintainers. Inherited display artwork retains
the original Lopaka attribution. See [complete credits and license evidence](docs/CREDITS.md)
and the retained component notices before distributing source or binaries.
