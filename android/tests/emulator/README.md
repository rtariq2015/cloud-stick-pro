# Android emulator checks

This separate test APK exercises the **unmodified signed Viewer APK** through
standard Android instrumentation. It uses only Android SDK APIs; it adds no test
entry point or dependency to the production app. Its manifest is `testOnly`, it
has no launcher, and its runner rejects physical phones.

Use a disposable Android 16 emulator with a reachable configured test stick.
The probe changes only its emulator app address/permission state, toggles only the
emulator’s network, and uses a public dummy login for Bluetooth permission/empty
scan tests. It never knows the owner’s password. Real Wi-Fi status and virtual
Bluetooth scan results are different evidence. A completed scan with no virtual
stick does not establish a real radio connection or authenticated history.

## Build and run

Use the pinned JDK 17, Gradle 8.13, AGP 8.13.2 and SDK 36 environment described in
[the Android guide](../../README.md). From `android/`, build with:

```sh
./gradlew -p tests/emulator --no-daemon assembleRelease
```

Align and sign the test APK **with the same signing identity as the APK under
test**. For local source builds, use your own key consistently for both APKs.
The official release key stays private; neither key nor signed privileged test
APK belongs in GitHub/public releases. The test source/configuration carries the
scoped grant in [the component notices](../../THIRD_PARTY_NOTICES.md).

Use an explicitly selected emulator serial on every ADB command. Verify
`ro.kernel.qemu=1` before changing state. Install/update the Viewer and install
the test APK with `adb -s EMULATOR install -t`. On a **disposable emulator only**,
clear the Viewer’s data before the first suite to restore its initial state.

```sh
adb -s EMULATOR shell am instrument -w -e phase suite -e address PRIVATE_IP org.cloudstick.viewer.qa/.EmulatorProbe
adb -s EMULATOR shell am force-stop org.cloudstick.viewer
adb -s EMULATOR shell am instrument -w -e phase persist -e address PRIVATE_IP org.cloudstick.viewer.qa/.EmulatorProbe
adb -s EMULATOR shell svc bluetooth enable
adb -s EMULATOR shell am instrument -w -e phase bluetooth -e address PRIVATE_IP org.cloudstick.viewer.qa/.EmulatorProbe
```

Replace `EMULATOR` and `PRIVATE_IP` with your test device and its selected local
address. The `suite` phase expects an idle 0.4.6-cache-dev test stick with recorded
results; it checks 21 conditions. `persist` checks one condition after a fresh
process. `bluetooth` expects an enabled **virtual** controller without a matching
virtual Cloud Stick and checks four conditions, including Android’s actual
permission dialog. Run phases in that order; the main suite first denies the
permission, then the Bluetooth phase grants it. Permission labels are English.

All phases must return `passed:true` and `INSTRUMENTATION_CODE: -1`.
`INSTRUMENTATION_CODE: 0`, a missing report, or a deadline is failure. Retain raw
output privately and publish only reviewed name-free evidence. The probe observes
native replies for the empty scan; it does not substitute a Bluetooth/HTTP driver,
disable CSP, remove FLAG_SECURE, enable remote debugging or bypass authentication.

[Recorded emulator and physical-stick scope](../../../docs/releases/2026-11-08-android-emulator.md).
