# Guided companion development — 8 November 2026

Setup 0.7.3 and Android 0.4.3-dev (code 10) add automatic USB detection, a computer
live overview, signed companion-app update checks/downloads, native installation
handoff, optional startup detection and Android Wi-Fi/cloud settings controls.
Firmware 0.4.8-discovery-dev adds a bounded local address-discovery responder.

## What has been checked

The desktop address reader was tested against the owner's actual running
0.4.7-wake-dev stick: it obtained the station address over USB and read public LAN
status, with five verified records and zero pending/failed. No firmware, settings
or user-file write occurred in that check. A strict wire-format mismatch was
corrected to preserve the full 64-bit byte counters across the native UI bridge.

Independent host socket tests exercise nonce/length/subnet validation, memory and
lifecycle gates, and 100 discovery socket release cycles under sanitizers. The
existing HTTP dashboard and synthetic owner-loop tests remain separate from
physical acceptance. The new discovery responder introduces no new RTOS task or
retained application buffer. Its socket is closed with the dashboard before BLE;
existing memory-recovery gates remain enforced.

The current local run has 217 native cases (216 passed and one platform-specific
case skipped), 241 passing JavaScript tests and 63 passing JVM tests, plus an
Android release build/lint and 60 actual-asset synthetic render assertions.
The private credential-directory fixtures ran with their required private umask;
packaging tests used the normal umask. A first combined run used the wrong fixture
umask and failed permission admission; the correctly separated runs passed.
Local app checks cover native update signature/hash/revision handling, unchanged
authentication vectors, exact bridge operations, hotplug identity changes,
background cancellation, explicit Wi-Fi saves and ambiguous-save cleanup. Android
release build, JVM tests and lint run without signing credentials in the source.
The desktop window smoke test uses a synthetic transport with no hardware access.

The frozen-SDK build verifies all 6,509 reference entries before and after, with
unchanged partitions and USB storage ownership. Reports retain exact input and
artifact hashes outside the public repository. A pre-shutdown result does not
prove the final changed source was built; the final source is checked again.

## New layout and local regression fixes

The computer companion now uses separate Overview, My stick, Guided setup and
Updates screens. Authenticated management has Overview, Wi-Fi, Cloud and History
tabs. Android uses matching mobile navigation and management tabs. Navigation
cannot interrupt an admitted desktop operation or hide a live authenticated
session behind another main screen. The shared authenticated Wi-Fi client can
stage removal of selected saved profiles, check a retained enabled network and
save only after a separate explicit action. These removal checks are simulated;
no owner's real saved network was removed for testing.

A real-phone test caught two missing local module allowlist entries in 0.4.0-dev.
Version 0.4.1-dev explicitly admits settings.mjs and updates.mjs; a JVM regression
checks every packaged executable asset against the fixed local allowlist. No
remote-page capability or authentication relaxation was added.

## Exact-image physical checks

Firmware 0.4.8-discovery-dev was built from
`f25d8a5e30df7be7dafd5a5fe2ef736a5c8ac11b`. Its application SHA-256 is
`929fe7a85a0426eaac5a044d0b21d47bd543765d09011700bb495542f121f329`.
The protected existing-stick install made two fresh matching encrypted 16 MiB
recovery reads, reopened them, wrote only the application and verified full
preservation, normal startup and the saved owner identity. The worker's observed
swap limit and current usage were both zero. No layout, eFuse or user-file change.

The official-signer Android 0.4.1-dev APK is 2,830,262 bytes, SHA-256
`a2156c6171ff21f535576ddfeac0e6745274404db0c6c7a2fbda2ef5f194b68d`.
Installation retained app data and the signing identity; the pulled installed
base.apk matched. Two actual-phone assertions passed: automatic discovery saved
the exact wired stick's address, and the new firmware's live dashboard loaded.
Desktop USB address reading also passed. Direct unicast discovery worked; this
computer's enabled firewall logged blocked broadcast replies. Its rules were
not changed. Network isolation and firewall restrictions remain real limits.

The retained-trust private-phone test reached authenticated record reading but
failed before private results were shown. Its cause is under investigation.
Cleanup independently confirmed Bluetooth closed, memory recovered, Wi-Fi and
backups resumed, no reset, the same saved Wi-Fi revision and five verified / zero
pending / zero failed records. This is a failed private-results test, not full
Android acceptance. No saved profile, owner login, trust pairing or user file was
changed. Private evidence and temporary helpers are not release inputs.

## App-side timer correction

The failed private result read was narrowed to an explicit native disconnect
(`backup_status` returned the fixed `cancelled` transport code). The native
companions intentionally pass `sessionTimeoutMs: null`; the shared client passed
that null directly to `setTimeout`, which schedules an immediate callback. The
callback closed an authenticated session as the first record request started.
The shared client now skips only that optional app-side timer for explicit null.
The browser default and explicit finite deadline still close their sessions;
firmware window limits, foreground cleanup, authentication and every transport
bound remain enforced.

The regression uses the real shared connection and native GATT adapter with
asynchronous record replies. It fails against the preceding committed source
and passes with the fix. A second regression proves a finite deadline still
closes the link. The identical fix is bundled in desktop and Android. Setup
0.7.2 also reveals the Updates screen when restoring an interrupted firmware job
and clears an installed-version label when the selected USB changes.

Android 0.4.2-dev/code 9 is 2,830,262 bytes, SHA-256
`4869a90654ab693eaa82ad43680a9917e2155cc97f645b84bb554411fafe6ecc`.
The installed base.apk matches, with the official signer and retained app data.
Thirty actual-phone assertions now pass on this exact APK with firmware 0.4.8.
The existing trusted pairing survived both app and firmware updates. Three signed
native Wi-Fi wakes required no new USB/button window; each authenticated, read
bounded private results and closed. Saved Wi-Fi reading and a real nearby scan
passed, as did Refresh and an activity restart. A forged proof returned 403
without pausing backups. Three close checks and final independent USB diagnostics
confirmed memory restored, Bluetooth closed, Wi-Fi/backups resumed, unchanged
saved Wi-Fi revision and no reset. No owner login, profile, trust pairing or user
file was written. The private phone helper and transient login were removed;
normal Viewer reopened without data clear.

The earlier five-file prerequisite stopped a separate run before BLE when it no
longer matched the device. Read-only metadata on the exact USB volume showed the
QA directory empty; no file content was read or changed. The successful test used
that observed zero-count baseline and preserved it. Earlier five-file checks
remain historical; this does not explain the folder's changed contents, prove
five files are still present or establish a new cloud upload. The preceding
0.4.1 failures remain recorded above.

## Existing-app upgrade cache migration

Installing the newer desktop package over an older Setup can leave an older
signed firmware catalog cached privately. The app now validates that cache, keeps
the newer signed bundled baseline when the cache is older, and still adopts a
newer valid cache. Corrupt signatures remain rejected; online revision rollback
checks are unchanged. Three native regressions cover those cases. The older-cache
regression fails against the preceding source and passes with the fix. No recovery
copy, settings record or user file is modified by this catalog selection.

## Signed offline firmware data

The normal-only revision-5 firmware catalog and package are published at
[firmware-data-0.4.8-r5](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/firmware-data-0.4.8-r5).
All five public asset URLs and SHA-256 digests were checked with fresh unauthenticated
downloads; all six packaged images were verified again. The preferred firmware
source export contains 1,055 files, each verified against its manifest. Catalog
SHA-256: `2030aa2578d304fb3e526a74b2ba364798fcd48723f4216aa4e760cca0b080fb`.
The package is 1,128,090 bytes, SHA-256
`5e37d4b03f9eb24800cfc4e7166ea26e27e84ceb5f7f6e69f8ab6402fac5ae5d`.

The final computer package is configured to include this verified discovery firmware for offline
installation. Its independently authored companion notice is synchronized with
the reviewed source grant; a packaging regression prevents those copies drifting.
Private observer firmware and recovery copies are not included. This data release
alone did not promote the main app-update feed or silently install anything.

## Acceptance still required before promotion

Exact-image settings saves and update-install round trips remain separate
physical checks; the private-results recovery above is now accepted. Native Windows CI does not establish
physical Windows USB or Bluetooth acceptance. First installation on an
unconfigured original board, actual AP power-off, fresh cloud upload, password
revocation and endurance remain separate pending tests.

The older intermittent BLE memory-recovery fault remains open in issue #5. A
passing connection or discovery test is not evidence that it was fixed. Earlier
27-check trusted-phone acceptance belongs to firmware 0.4.7 and Android 0.3.0;
it is not silently transferred to these new images.

This development note does not promote a new default firmware, APK or desktop
installer by itself. Release assets and the signed update feed are published only
when their exact builds and applicable checks have been recorded. Private device
logs, personal network names, credentials, keys and recovery copies are excluded.

[Plain-language guide](../COMPANION_APPS.md) · [Credits and release gate](../CREDITS.md)

## Deferred startup update check

Testing the installed Linux 0.7.2 app with a real stick connected exposed a
startup race: the firmware check owned the bridge when the app update check
started. The latter silently skipped its only attempt. The manual Check again
button worked. Setup 0.7.3 and Android 0.4.3-dev/code 10 now defer the initial
check until the app is idle and visible, then make one attempt. Network failures
remain visible and retry requires the user; shutdown removes deferred work.
Sixteen JavaScript regressions cover both actual controllers, including busy
startup, visibility, manual checks, shutdown during a request and state polling.
The 30 authenticated phone checks above belong to the exact 0.4.2 APK; they are
not an assertion of new-APK physical acceptance.

The synthetic full-installer fixture now selects the signed catalog's actual
latest package and expected boot version, instead of a hard-coded 0.4.6 file.
The stale pairing correctly failed closed after the catalog update. With the
fixture corrected, all 217 native cases ran: 216 passed, one platform-specific
case skipped. No production installer or recovery guard was relaxed.
