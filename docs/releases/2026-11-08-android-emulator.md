# Android Viewer 0.2.1 — tested on a developer phone in Zorin

The owner asked for a developer-style phone on the computer and thorough,
focused checks. An isolated Android 16 emulator is now installed on the Zorin
computer, with a **Cloud Stick Test Phone** app-menu launcher. The installed
phone app is the signed Viewer APK, with its normal security protections.

## What changed in the app

Two small problems were found during review and emulator testing:

- An invalid address was correctly rejected by the native validator, but its
  request ID was lost before the error reply. The app waited for its 20-second
  request deadline before showing guidance. A well-formed positive ID is now
  retained before semantic validation, so rejection gets an immediate matching
  error. Duplicate/malformed IDs and invalid operations remain rejected.
- Initial connection failure could start generic cleanup before the rejected
  connection promise supplied its useful error. During that initial attempt,
  the caller now handles the error and uses the existing fixed guidance. Later
  disconnections still clear private content immediately. No arbitrary native
  error or password is displayed.

The update is **0.2.1**, Android version code **5**. It retains the same app ID
and signing identity. Firmware stays **0.4.6-cache-dev**, desktop Setup **0.6.0**.
No firmware, BLE driver/session, shared crypto/protocol, partition or user-file
change was needed. No firmware installation was performed.

[Download the tested Android preview](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-android-tested)
and choose **Cloud-Stick-Viewer-0.2.1.apk**. Open it on your phone and choose
**Update**. The saved stick address remains the normal way to open live status.
The computer emulator is already installed locally; it is not a second phone APK.

## Tests and their actual scope

| Test path | Result and scope |
| --- | --- |
| Signed Viewer on Android 16 emulator → real stick over the LAN | Live status, saved address, immediate invalid-address response, offline/restore, background cancellation, private-view permission denial and return-to-dashboard checks pass |
| Signed Viewer on Android 16 emulator → virtual Bluetooth controller | Android Nearby devices permission, native empty scan, hidden private results and cleanup-to-dashboard checks pass; this is not a physical radio link |
| Zorin’s normal Bluetooth service → real USB-matched stick | Real discovery, identity match, GATT service/characteristics, protocol metadata read and disconnect pass; owner sign-in was not attempted |
| Emulator → computer’s physical Bluetooth controller directly | Not completed: Bumble’s raw USB controller access returned `LIBUSB_ERROR_ACCESS`. The attempt stopped and host Bluetooth was restored; no privilege/security workaround was added |
| Real Android phone → this exact APK | Still pending; the emulator is not a replacement for that acceptance |

The Android suite checks **21 conditions**, a separate process restart checks
one, and the native virtual-Bluetooth phase checks four: **26 emulator checks**.
These use standard, same-key signed instrumentation of the unmodified release
APK; no fake HTTP or GATT implementation is substituted. The test app is separate,
`testOnly`, has no launcher and refuses physical phones. Its
[source and instructions](../../android/tests/emulator/README.md) are public;
its signed APK and the release signing key remain private.

Other checks: **49 Java unit tests**, **60 JavaScript tests**, **56 synthetic
rendered-layout checks** at two widths, release compilation, Android lint, and
publication/export-policy checks. The unchanged WebView-JavaScript and retained
notice-resource lint warnings stay documented; no error/check is suppressed.
The release validation file identifies the exact final APK/source, counts,
emulator version, system image and WebView version.

Actual environment: Zorin OS 18.1, x86_64/KVM; Android Emulator **37.2.13.0**;
Android 16/API 36 Google APIs x86_64 system image revision **7**; Android WebView
**133.0.6943.137**. Android emulator display checks are separate from the earlier
Qt/Chromium synthetic previews.

## Stick and data protection

The initial read-only USB diagnostic snapshot found normal firmware, Wi-Fi and
the dashboard running, with five verified records and no pending/failed files.
The local address was read from the selected USB instead of trusting a typed
address. Raw snapshots, addresses, controller identity and device logs stay private.

For the separate real Bluetooth check, the existing local USB bench command opened
the normal bounded setup window. Its normal identity was matched over Bluetooth
before reading protocol metadata. Settings and private history still required
owner authentication. No credentials, settings or user files were written.

The peer disconnect begins the existing firmware close lifecycle. A subsequent
close request was not accepted in that already-closing/closed phase; exact USB
diagnostics then confirmed BLE off, Wi-Fi connected, backups resumed, dashboard
listening, unchanged verified-record count and no device reset. An acknowledgement
alone was not used as proof of recovery. This is not a new cloud upload test.

## Remaining limits

- Actual phone installation, OEM permission behavior, hotspot routing and an
  authenticated private-history round trip still need the owner’s phone/login.
- Emulator networking uses the computer’s LAN route. It does not prove a physical
  Wi-Fi access-point outage or the phone’s hotspot radio behavior.
- Always-available/button-free Bluetooth and a full Android settings editor are
  future firmware/app work; this test update does not claim to implement them.
- Backup storage limits and the requirement for a protected setup window remain.
  The physical stick was not erased or used to simulate a new board.

Original attribution, component notices and the upstream license-text gap remain
in [Credits](../CREDITS.md). Source/binary checks remain separate from physical
acceptance. No private logs, keys, live address or signed test helper is public.

Platform references: [Android emulator command-line operation](https://developer.android.com/studio/run/emulator-commandline),
[virtual radio networking](https://developer.android.com/studio/run/emulator-networking-advanced),
and [Google Bumble’s documented Android bridge](https://google.github.io/bumble/platforms/android.html).
