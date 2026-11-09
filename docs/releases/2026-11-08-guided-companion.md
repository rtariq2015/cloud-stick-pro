# Guided companion development — 8 November 2026

Setup 0.7.0 and Android 0.4.0-dev (code 7) add automatic USB detection, a computer
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

The current local run has 210 native cases (204 passed and six platform-specific
cases skipped), 211 passing JavaScript tests and 62 passing JVM tests, plus an
Android release build/lint.
Local app checks cover native update signature/hash/revision handling, unchanged
authentication vectors, exact bridge operations, hotplug identity changes,
background cancellation, explicit Wi-Fi saves and ambiguous-save cleanup. Android
release build, JVM tests and lint run without signing credentials in the source.
The desktop window smoke test uses a synthetic transport with no hardware access.

The frozen-SDK build verifies all 6,509 reference entries before and after, with
unchanged partitions and USB storage ownership. Reports retain exact input and
artifact hashes outside the public repository. A pre-shutdown result does not
prove the final changed source was built; the final source is checked again.

## Acceptance still required before promotion

New exact-image physical discovery, Android settings saves and update-install
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
