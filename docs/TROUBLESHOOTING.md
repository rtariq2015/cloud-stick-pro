# Help with Cloud Stick

Start with the problem you see. Keep the USB connected unless a setup or
installation step specifically asks you to unplug it.

## Which app do I need?

| Name | Use it for |
| --- | --- |
| **Cloud Stick Setup** on Windows/Linux | Firmware installation/update, your login, Wi-Fi, cloud settings and diagnostics |
| **Cloud Stick Viewer** on Android | Live Wi-Fi dashboard; Bluetooth sign-in for private history |

Use the [project's downloads](../README.md#download-the-preview). Firmware is the
software inside the stick; a `.bin` or `.cspfw` file is not a phone/computer app.
**Code → Download ZIP** is for project source.

## The Android dashboard cannot connect

Use Viewer 0.2.1, choose **Live dashboard**, and enter the current IP address shown
on the stick under **Stick connection**. Use the address that works in your phone’s
browser. Keep the phone and stick on the same reachable Wi-Fi or hotspot; guest
networks can block access between devices. An IP can change when networks switch.
No BOOT, Bluetooth or login is needed for this view. Old readings stay marked as
old until a new status arrives. **Private history** is a separate Bluetooth view.

## The computer app cannot find the USB

Plug in one supported original T-Dongle S3 and press **Find my USB**. Select it,
then press **Check this USB**. Close serial monitors and other copies of the app.

For ordinary setup, reconnect normally. Use the BOOT-while-plugging-in step only
when the firmware installer asks for it. A board in installation mode cannot
answer the normal setup check.

On Linux, the `.deb` installs the narrow USB access rule. Run the app from your
normal desktop account and reconnect if permissions were just installed. The
portable package needs its documented system dependencies/access setup.

## Wi-Fi or hotspot will not connect

Keep the hotspot on, find it in the signed-in setup wizard, check its connection,
then save. Keep the saved password when offered. A hidden network needs its
explicit hidden-network entry.

If the hotspot is absent, check the phone's Wi-Fi compatibility/band settings.
The original ESP32-S3 uses [2.4 GHz Wi-Fi](https://www.espressif.com/en/products/socs/esp32-s3); a 5 GHz-only hotspot will not appear.
The current setup flow does not support enterprise/802.1X or WPA3-only networks.

A saved network is not proof of internet access. A working home connection also
means the stick has no reason to switch just because a hotspot was turned on.
Saved-network alternatives are tried after a disconnection; retries can take time.

Controlled switching between two saved networks passed on the recorded 0.4.6
service image. A real access-point power-off test and the owner's exact phone
Save/Finish sequence remain unverified. [Detailed evidence](releases/2026-11-06-ble-cleanup.md).

## BLE needs review after Finish

BLE means the Bluetooth setup connection. This message needs a status check;
it does not by itself prove that the Wi-Fi password was lost.

1. Leave the USB plugged in and allow its current cleanup to finish.
2. Close the setup connection. Reopen **Cloud Stick Setup**, find/select/check
   the USB, then open **Troubleshoot / updates → Diagnose my stick**.
3. Follow the reported next step. Use **Copy safe diagnostic JSON** if it still
   needs review. Keep that report separate from private backup records.

Avoid repeated Save clicks or a factory reset. The current firmware improved
Bluetooth cleanup and passed the recorded close checks, but the exact personal
phone Save/Finish issue is not marked fully accepted. Preserve the original files
and report what you pressed and what appeared.

## Files have not backed up yet

Finish Bluetooth setup, leave power connected and let file copying settle. The
default quiet wait is two minutes; a saved schedule can defer the backup.

Check the watched folder (`QA` by default), saved Wi-Fi and cloud destination.
Use a Koofr app password. Read **Pending**, **Failed** and the saved error/result.
Upload at 100% still needs a cloud-copy check before **Verified**.

New computer writes can interrupt a check and make it retry later. A recorded
Verified date belongs to that particular file version. Keep your local original
until you have checked your own cloud copy.

The default backup size limit is 64 MiB per file, and the persistent backup
inventory tracks 128 paths. The Android screen's eight summaries are a smaller
view. These are backup/view limits, separate from the card's ordinary storage.

## New-board installation is blocked

Confirm the board is the **original 16 MB T-Dongle S3 without PSRAM**. Remove the
microSD for a first installation. If a USB drive is shown, safely eject it first.
Hold BOOT while plugging the board back in, release BOOT, then find/select the
USB in the installer again.

The app blocks unknown firmware, another board, existing ownership or unexpected
internal data. Use the existing-stick update path for an already configured
Cloud Stick. First installation on a new board still needs physical testing;
do not erase an owned stick to imitate a new one.

## An installation was interrupted

Reopen the app on the same computer. It reads the saved job and does not start a
new write automatically. Use **Review and resume interrupted install** only for
the same stick, following its BOOT instructions. Keep the private recovery copies.

Changed protected data or a damaged checkpoint blocks recovery. If bytes were
verified but normal startup needs review, reconnect normally and use diagnostics.
Do not keep reinstalling just to clear a startup-review message.

## I see two copies of the app

Close the app and use [installation/removal help](../desktop/README.md#update-or-remove-an-older-copy).
Keep one installation type. The computer setup app and Android viewer have
different jobs; their names do not identify two firmware versions.

## Share a useful problem report

Open [Report a problem](https://github.com/rtariq2015/cloud-stick-pro/issues/new/choose).
Include the app/firmware version, what you clicked, what happened and safe
diagnostic JSON if available. Programming knowledge is not required.

Leave out passwords, keys, recovery copies, private file names and clinical files.
A screenshot is useful only after private information has been removed.
