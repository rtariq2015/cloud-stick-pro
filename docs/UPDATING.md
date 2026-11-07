# Getting Cloud Stick Pro updates

You do not need to know how to code to follow the project. Use this repository's
[release notes](RELEASE_NOTES.md) to see what changed, and
[Releases](https://github.com/rtariq2015/cloud-stick-pro/releases) for downloadable
packages. The current download is a **development prerelease**, not a stable
release. Choose **Watch → Custom → Releases** on GitHub if you want release
notifications.

## What is available now

The public repository contains the complete reviewed development source: firmware,
Linux/Windows setup app, Android viewer, tests, guides and maintenance tooling.
Use **Code → Download ZIP** for the source, or `git pull` to update an existing
checkout. [Preview 2026.11.06](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.06-wifi)
provides these separate downloads:

| Component | File and compatibility |
| --- | --- |
| Linux setup **0.5.4** | `.deb`, or alternative `.tar.gz` user package; Ubuntu 24.04 x86-64 / glibc 2.39 or compatible |
| Android viewer **0.1.2** | Signed APK for Android 12+; exact-version physical phone acceptance is pending |
| Firmware **0.4.4-wifi-dev** | App-only `.bin` for a reviewed existing compatible Pro layout; not a factory installer |
| Windows **0.5.4** | Shared source/build recipe only; no native Windows executable download |

Android 0.1.2 remains in the preceding `preview-2026.11.06` release.

The release includes **INSTALL.md**, **SHA256SUMS** and **release-manifest.json**.
Firmware, desktop and Android have separate version numbers. Downloading or
installing one component does not update the others.

The original [Cloud Stick by John / JMcrafter26](https://github.com/JMcrafter26/cloud-stick)
states MIT in its README, but its linked license grant file is missing from the
reviewed upstream tree. The source preview retains this unresolved evidence and
all available notices; no substitute upstream license has been invented.
GitHub's source ZIP is the project source, not an installer. Building the guarded
product firmware still needs the pinned SDK inputs described in the developer
guide. A source download does not install anything on your dongle.

## Install or update a preview component

1. Open [Wi-Fi fallback preview](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.06-wifi)
   and read **INSTALL.md** and the known-limit notes. A prerelease is a test
   candidate. There is no stable binary release yet.
2. Pick the package for your component and operating system. The firmware target
   is the **original LILYGO T-Dongle S3, 16 MiB flash, no PSRAM**; a generic USB
   drive, C5 or S3 Plus is not interchangeable.
3. Compare the download's SHA-256 with the release manifest. Keep a copy of the
   previous installer and follow the release's recovery instructions.
4. Close the desktop app before replacing it. Android updates must retain the
   existing signing identity. Installing an app does **not** update the dongle.
5. Firmware requires its own reviewed, app-only procedure and current private
   recovery backup. The desktop's current firmware feature reviews a manifest;
   it does not provide a general update writer. Do not erase all flash, format
   microSD or replace settings/receipts as an ordinary update.

Nothing in this GitHub workflow remotely updates or restarts your device. There
is no automatic migration of your account, Wi-Fi or cloud settings.

The prerelease does not add hardware acceptance: packaged-GUI physical
interaction, the exact Android APK on a phone, native Windows, actual venue
portals, physical LCD appearance and multi-day endurance still need their own
checks. Earlier device results apply to their recorded images and conditions.

## If something goes wrong

Open [Issues → New issue](https://github.com/rtariq2015/cloud-stick-pro/issues/new/choose),
choose **Report a problem**, and describe the few steps that caused it. Include
the component/version and error text. Remove passwords, owner cards, private
keys, backups, clinical files and private history before sharing any evidence.

A failed upload and a verified backup are different outcomes. Preserve your local
originals. Do not install an older firmware over a live device unless its exact
configuration/receipt compatibility and rollback procedure have been reviewed.

See [how maintenance works](MAINTENANCE.md) for automated checks and their limits.
