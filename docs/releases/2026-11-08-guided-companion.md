# Guided companion development — 8 November 2026

Setup 0.7.1 and Android 0.4.1-dev (code 8) add automatic USB detection, a computer
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

The current local run has 213 native cases (212 passed and one platform-specific
case skipped), 223 passing JavaScript tests and 63 passing JVM tests, plus an
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

## Acceptance still required before promotion

Exact-image private-results recovery, Android settings saves and update-install
round trips must be recorded separately. Native Windows CI does not establish
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
