# Get started with Cloud Stick Pro

This guide is for the **original LILYGO T-Dongle S3** running reviewed Cloud Stick
Pro firmware. It is not an installation recipe for a generic USB drive or an
unknown stock board. Read the [current version status](../README.md#versions-and-evidence)
before choosing an application or firmware image.

## 1. Check what you have

You need the supported dongle, a healthy compatible microSD with a supported
filesystem, an authorized Wi-Fi network and an HTTPS WebDAV destination.
Koofr requires your account identifier and a dedicated app password; your
personal Cloud Stick login is a separate credential.

An existing owned dongle keeps its current login. The app does not supply a
factory `admin/admin` login or reset someone else's owner record. A genuinely
unowned board with reviewed Pro firmware has a distinct first-owner flow with
physical checks. Stock-board firmware installation and generic firmware updates
remain gated; opening the desktop app does not install firmware.

For a working device, retain your files and configuration. Do not format a card,
reset settings, hold unfamiliar long-button sequences or flash a factory image
to get past a setup error. Use the displayed help and the release's reviewed
recovery procedure instead.

## 2. Open the desktop setup app

Use the reviewed Linux bundle supplied with the release. Launch **Cloud Stick
Setup** from the applications menu after installation, or run the complete
portable application directory. Keep its `_internal` directory together with
the executable. [Desktop instructions](../desktop/README.md) cover dependencies,
installation, removal and running from source.

Plug in the dongle normally and follow the wizard:

1. Use the USB step to find connected devices, choose the correct one and check its status.
2. Enter your existing personal username and password. A previously trusted Linux
   computer may offer remembered setup access for that exact owned dongle.
3. Use **Create my login** only when the relevant existing-access or genuine
   first-owner flow is available. Choose and repeat a memorable password phrase
   that meets the displayed policy.
4. Review the proposed login change and explicitly save it. Reconnect using the
   new login before continuing. Clicking Next alone does not save a change.

First-owner enrollment may require removing the card and holding BOOT continuously
for the firmware's indicated interval. Follow that exact screen; do not remove
media while the host or firmware is using it. Ready/Fault owner records are not
an invitation to repeat enrollment. An uncertain save needs a status check or
fresh sign-in, not repeated clicks on Save.

USB selection/status assistance and Bluetooth settings are different paths.
The app may open the setup window over supported USB controls; otherwise it
explains the required BOOT action. Merely pairing through the operating system's
Bluetooth Settings does not complete the application's Security 2 login.

## 3. Choose Wi-Fi

In the authenticated wizard, select **Find Wi-Fi networks** and choose a network
you are allowed to use. Enter its password, or explicitly mark an open network
as having no Wi-Fi password. Hidden-network entry remains available.

Check the connection, review the intended settings and save only what you want
to change. Retain an existing saved password when offered instead of retyping it.
Successful local Wi-Fi association is separate from Internet or cloud access.
Enterprise/802.1X and WPA3-only setup are outside the current supported flow.

The Pro setup firmware can remember up to eight saved networks. To add another,
repeat Find Wi-Fi, Check and Save in the authenticated desktop wizard. Saving a
different network keeps the earlier entries. After setup closes, the dongle can
try another enabled saved network if its current Wi-Fi disconnects; it keeps a
working connection rather than switching just because another network appears.
There can be a delay while a connection attempt times out. The
[0.4.4 update](releases/2026-11-06-wifi-fallback.md) introduced fair retry order.
The current [0.4.6 update](releases/2026-11-06-ble-cleanup.md), supported by Setup
0.5.7, passed controlled switching between two saved networks and back. Both
access points stayed on; real AP outages and the exact personal phone Save/Finish
flow remain separate acceptance steps.
The Android viewer does not add Wi-Fi credentials.

Some libraries, workplaces and cafés use an open network followed by a website
where you accept terms. That is a captive portal. Assistance in this project is
experimental: the person must accept the terms, the venue must permit the route,
and the **dongle's** post-login access must be checked. Sessions may expire, so
acceptance is not guaranteed to be a one-time action. Start with a known personal
network or authorized hotspot when testing basic backups.

## 4. Choose the cloud destination

Use the cloud step to enter your HTTPS WebDAV endpoint, account and dedicated
app password. The Koofr preset helps with its endpoint/folder settings. Keep
the cloud credential separate from your personal Cloud Stick login.

Read the connection-check result, then review and explicitly save intended
changes. A saved configuration is not proof that any file has uploaded. Avoid
changing production folders/accounts during a first synthetic test.

## 5. Finish and try a small file

Use **Finish** or the interface's disconnect action to close the Bluetooth setup
session. Keep the dongle physically plugged in. The backup worker can resume
after radio/setup cleanup succeeds.

If the display reports **BLE needs review**, open **Troubleshoot / updates →
Diagnose my stick** in Setup 0.5.7 after disconnecting setup. It explains the
reported state and a next step. If the warning remains, use **Copy safe diagnostic
JSON** for review and keep the dongle connected. Do not repeatedly re-save Wi-Fi
or reset settings to clear the warning.

Copy a small disposable text or PDF file into the configured watched folder
(`QA` by default), finish the copy and wait. The default quiet period is two
minutes without USB writes; a saved overnight schedule can defer the pass.
The dongle works from the file data already on the card. It cannot flush an
application's unsaved memory, and later host writes can interrupt/restart checking.

Watch the sequence: **finding files → checking a local file → uploading → cloud
verification → saved receipt**. Uploading 100% is earlier than verified success.
For an initial acceptance test, independently inspect the disposable cloud copy
before trusting a new image with important files.

## 6. View results

| Interface | How to open it | What it tells you |
| --- | --- | --- |
| Dongle LCD | Look at the connected dongle | Current phase, counts, measured rates and waiting/fault state |
| Local dashboard | Open the current HTTP address shown on the LCD, from the same reachable network | Read-only device status and recorded backup counts; enter no credentials here |
| Desktop app | Open the status/records view, or choose Diagnose my stick under Troubleshoot / updates | Authenticated bounded records or a typed USB health snapshot, depending on the view |
| Android viewer | Open a setup window, sign in with your personal login and explicitly select your nearby dongle | Recorded status and up to eight saved file summaries |

The Android interface is bundled in the app and needs no hosted website or
Internet permission. It still needs Nearby devices permission and a real BLE
connection. Actual phone acceptance is tracked separately from building an APK.
Tap **Done — Close Bluetooth** when finished. The stick decides when backups can
resume after cleanup checks succeed. Backgrounding/closing a client clears its
authenticated session; returning requires fresh state.

Recorded Verified means that a particular file version was checked at the shown
time. It does not mean the device just contacted Koofr, all current host writes
were scanned, or the remote copy can never be removed. Partial scan/history flags
and pending/failed counts matter. The queue has 128 persistent path identities;
the eight-row Android summary is a smaller view, not an unlimited file browser.

If something fails, keep the original files, note the fixed error code and read
the relevant help. A lost HTTP connection does not prove the backup stopped.
For useful bug reports, follow [Contributing](../CONTRIBUTING.md); keep keys,
passwords, recovery files and sensitive file names out of reports.
