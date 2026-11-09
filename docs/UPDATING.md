# Get Cloud Stick updates

[Open the current Setup 0.7.3 preview](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-guided-companion).
Choose the Windows installer or Linux `.deb` for **Cloud Stick Setup**. Portable
alternatives are available on the same page. Programming knowledge is not required.

## What gets updated?

| Software | Where it runs | How to update it |
| --- | --- | --- |
| **Cloud Stick Setup** | Windows/Linux computer | In Updates → App updates, download and choose Install update |
| **Firmware** | Inside the USB stick | In Setup, choose **Install or update your stick**, check the release and confirm installation |
| **Cloud Stick Viewer** | Android phone | In App updates, download and confirm Android installation |

Installing the computer app updates the program on the computer. Firmware changes
when you choose and confirm its installation inside Setup. Future compatible
signed firmware can be downloaded through that screen. A release needing a newer
Setup app tells you to update the app first.

## Update the computer app

1. Open **Updates → App updates**. Setup checks the signed release when idle,
   or you can choose **Check again**.
2. Choose **Download update** and wait for the size and hash checks.
3. Choose **Install update**. Windows opens the verified per-user installer.
   A Linux user installation replaces its program folder and keeps the old one;
   a system `.deb` uses the computer's package installer and may ask for your
   computer administrator password.
4. Reopen **Cloud Stick Setup**. Settings and private recovery copies are retained.

For a manual download, close Setup after any firmware work finishes, open
[the release](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-guided-companion),
and choose the package for your computer. Keep the old download until the new
app opens correctly.

The release includes checksums, source and test records. **Code → Download ZIP**
is source code, rather than an installer. If you see duplicate app entries, use
[update/removal help](../desktop/README.md#update-or-remove-an-older-copy).

## Update an existing stick

Open **Install or update your stick**, choose **Update my existing Cloud Stick**,
then **Check available firmware**. Find/select the normally running USB and press
**Pause backups for update**. Follow the guided eject, BOOT reconnection and
confirmation steps. Keep power connected through recovery checks and verification.

The app preserves the existing login, Wi-Fi settings, layout, internal backup
records and microSD files. If it already has the offered firmware, the app checks
and reports that without rewriting the same image. Recovery copies stay private
on this computer. [Full installer guide](INTEGRATED_SETUP.md).

## Set up a newly bought board

Use **Install on a new original T-Dongle S3** in the same app. It is a different
path from an ordinary update. Follow the BOOT and card-removal instructions, then
create your first account. After Wi-Fi/cloud setup and confirmed Bluetooth close,
return the microSD while the stick is unplugged if it is still out. Reconnect
normally and try a small disposable file.

The supported target is the original 16 MB T-Dongle S3 without PSRAM. First
installation on an unconfigured board still needs physical acceptance; the owned
stick was not erased to simulate it. An unknown or already owned board is not a
reason to repeat a factory installation.

## Android viewer

Open **App updates** in Viewer to check, download and choose **Install update**.
Android may ask you to allow installation from Cloud Stick, then confirm the
update. After allowing it, return to Viewer and tap **Install update** again.
You can also install **Cloud-Stick-Viewer-0.4.3-dev.apk** from
[the companion release](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-guided-companion).
The application ID and signing identity stay the same, so an in-place update
retains the saved stick address and trusted-phone pairing.

Open Viewer and choose **Find my stick automatically** on reachable local Wi-Fi.
The overview needs no login. **My stick** uses authenticated Bluetooth for
private history and saved Wi-Fi/cloud controls. Use **Done** to resume backups.
The phone does not install USB firmware. [Full Android instructions](../android/README.md).

## Preview limits and help

Native Windows/Linux builds, packaged-program checks and Windows per-user
install/launch/uninstall pass in CI. Those are software checks, rather than a
physical USB/Bluetooth test. New-board installation, physical Windows USB, the
exact personal phone Save/Finish sequence and multi-day endurance remain pending.
[Exact evidence](RELEASES.md).

Use [Troubleshooting](TROUBLESHOOTING.md) and share only safe diagnostics if
something fails. Keep original files and private recovery copies. GitHub can
notify you of releases through **Watch → Custom → Releases**; its scheduled
checks do not make an AI agent continuously operate your stick.

The [credits and release gate](CREDITS.md) retain the upstream license-text gap
and all component notices. No blanket replacement license has been invented.
