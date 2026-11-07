# Set up your Cloud Stick

You can do this through **Cloud Stick Setup** on Windows or Linux. Programming
knowledge is not required. [Download the computer app](../README.md#download-the-preview).

The preview is for the **original LILYGO T-Dongle S3**, with 16 MB flash and no
PSRAM. The Plus, C5 and ordinary USB drives need different software.

## 1. Get ready

Have your supported stick, a suitable microSD, a Wi-Fi network or phone hotspot,
and a Koofr account ready. Create a Koofr **app password** for the backup connection.
Your Cloud Stick login and your Koofr login are separate things.

Open the downloaded Windows installer or Linux `.deb`, then launch **Cloud Stick
Setup**. Keep the full application folder together if you use a portable package.
[Installation and removal help](../desktop/README.md).

## 2. Prepare the stick and your login

**New original board:** open **Install or update your stick**, choose the new-board
option and follow the [one-app installation steps](INTEGRATED_SETUP.md). If no USB
drive appears in the file manager yet, continue with the guide's BOOT reconnection
step. First installation on an unconfigured board still needs physical acceptance.

**Already configured Cloud Stick:** plug it in normally. Find, select and check
the USB in the wizard, then sign in with the username and password already saved
on the stick.

When **Create my first account** is offered, the app guides the card-removal and
continuous ten-second BOOT hold. Follow the screen, choose your own login and
keep a private copy. An existing account is preserved. There is no supplied
`admin/admin` login.

For a login change, review and save it, then reconnect using the new login. Next
moves between screens; the confirmation/save step makes the change.

## 3. Add Wi-Fi or your phone hotspot

1. Turn on the Wi-Fi network or phone hotspot you want the stick to use.
2. In the signed-in setup wizard, press **Find Wi-Fi networks** and select it.
3. Enter its Wi-Fi password, or keep the saved password when offered.
4. Press **Check Wi-Fi connection**, review the result, then **Save Wi-Fi settings**.
5. Keep the network or hotspot on through Finish and the backup check.

The stick can remember up to eight saved networks. Adding another keeps the
previous saved entries. After its current connection disconnects, it can try an
enabled saved alternative. It stays with a working connection, so turning on a
hotspot does not immediately move it away from working home Wi-Fi.

The Android viewer shows results; use the computer setup app to add Wi-Fi. If a
network is missing or will not connect, use [Wi-Fi help](TROUBLESHOOTING.md#wi-fi-or-hotspot-will-not-connect).
Venue networks that require a website sign-in remain experimental; start with a
known home network or your own hotspot.

## 4. Choose the cloud destination

Enter your Koofr email, Koofr app password and destination folder in the cloud
step. Review the proposed settings and explicitly save them. Use a dedicated
cloud app password, separate from the login you chose for Cloud Stick.

Saved settings mean the choice was stored. The stick checks its cloud access
and file backups after setup closes.

## 5. Finish and try a small file

Press **Finish — disconnect Bluetooth**. This closes the setup connection.
If you removed the microSD for the first account and it is still out, safely
eject any drive shown, unplug the stick, insert the card while unplugged and
reconnect normally. Your login and settings stay saved. Then **leave power
connected** for backups.

Copy a small disposable file into `QA`, the default watched folder, and finish
the copy. The default wait is two minutes without USB writes; your saved schedule
can postpone the backup. Keep the hotspot on if you are using it.

The stick finds the file, checks local contents, uploads, checks the cloud copy
and records the result. **Verified** means a particular file version passed that
check. Check the disposable file in your cloud account before relying on a new
setup for important files.

If **BLE needs review** appears, keep the USB connected and follow
[the Bluetooth help](TROUBLESHOOTING.md#ble-needs-review-after-finish). Repeated
Wi-Fi saves are not a useful way to clear that message.

## 6. Read the results

| Where | What to do |
| --- | --- |
| On the stick | Read its display for current work and recorded counts |
| Computer app | Open recorded backup results, or close setup and choose **Diagnose my stick** |
| Android | Open **Cloud Stick Viewer**, allow Nearby devices, choose your stick and sign in |
| Local dashboard | Open the current address shown on the stick from the same network; enter no passwords there |

For Android viewing, hold BOOT for three seconds on the normally running stick
to open its setup window. Tap **Done — Close Bluetooth** when finished. The first
account's ten-second hold and the installer BOOT reconnection are different
steps; follow the screen for the operation you are doing.

Recorded counts and times describe saved results, rather than a fresh cloud
check. Partial scans and pending/failed files still matter. See
[Troubleshooting](TROUBLESHOOTING.md) for help, or
[what has been tested](RELEASES.md) for the preview's limits.
