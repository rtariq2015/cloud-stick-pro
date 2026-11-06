# Cloud Stick Viewer for Android

Local Android 12+ companion for an already configured Cloud Stick Pro. Sign in with the same personal username and password used in Cloud Stick Setup. This app shows recorded backup status and up to eight saved file summaries. It has no key entry, settings editor, enrollment, firmware installer, website or Internet permission.

1. Open the stick's setup window by holding BOOT for 3 seconds. Android cannot wake an inactive stick radio.
2. Open the app, enter your personal login and tap **Find my Cloud Stick**. Allow Nearby devices when Android asks.
3. Choose your own advertised stick. The app never chooses the first result automatically.
4. Read the recorded status/history. Tap **Done — Close Bluetooth** when finished.

Bluetooth management pauses the backup worker. Recorded timestamps/counts are not a new cloud test, a live file listing or proof that a host application finished saving. History is bounded to eight summaries from the firmware's bounded inventory (currently 128 file entries). Flags report partial/faulted snapshots. Backgrounding the app closes its radio session and clears authentication/private results; returning requires a fresh login. There is no arbitrary two-minute app cutoff. The installed firmware still enforces its own idle and safety limits, and each native operation has a finite deadline.

Read-only describes this app's UI and command choices. Firmware authenticates an owner; it does not give this viewer a separate restricted owner role. No settings mutation commands are called. Security2 necessarily writes its handshake and encrypted request attributes.

## Build

Pinned: Java 17, Gradle 8.13, Android Gradle Plugin 8.13.2, compile/target SDK 36, Build Tools 35.0.0, AndroidX WebKit 1.14.0. No Kotlin/Compose compiler toolchain. Set `JAVA_HOME`, `ANDROID_HOME` and `GRADLE_USER_HOME` to your local tools/cache directories, outside this source tree. Native SDK signing tools also need the JDK `bin` directory on `PATH`.

Prepared input hashes: Temurin17.0.21+9 tar SHA256 `8b9b611d917e3dfd8419d3566b5949226f69b052fed7e109c1368add1dfe8711`; Gradle8.13 ZIP SHA256 `20f1b1176237254a6fc204d8434196fa11a4cfb387567519c61556e8710aed78`; Google command-line tools19.0 ZIP SHA256 `7ec965280a073311c339e571cd5de778b9975026cfcbe79f2b1cdcb1e15317ee` (upstream repository SHA1 `5fdcc763663eefb86a5b8879697aa6088b041e70`). JDK/Gradle matched upstream SHA256; command-line tools matched the official repository checksum. See [third-party notices](THIRD_PARTY_NOTICES.md) for runtime/test pins and inherited Security2 limits.

```sh
./gradlew --no-daemon :app:testDebugUnitTest :app:assembleDebug :app:assembleRelease :app:lintRelease
node --test tests/*.test.mjs
```

`assembleRelease` creates an unsigned, non-debuggable APK. The prepared local release APK is signed using a separate app-specific keystore outside Git/source/public artifacts. Its public certificate and APK hashes are recorded in `artifacts/validation/results.json`. Keep that private signing key to sign future updates; losing it prevents a same-identity in-place app update. Debug APKs are tests only. No phone installation or ADB operation is part of these build commands.

## Native boundary

Six bundled assets are served only at the exact `https://appassets.androidplatform.net/assets/` allowlist, through `WebViewAssetLoader`. Unknown/query/remote routes, external navigation, frames, popups, file/content access, downloads, cookies and Web storage are blocked. The main-frame message listener accepts only bounded strict scalar operations: permission, discover, connect, response-write, read and disconnect. No credential strings, paths or shell commands cross that bridge. No bridge requests or responses are logged. Screenshots/recents previews are blocked. The manifest disables cloud backup and extraction rules exclude app data from backup/device transfer; OEM transfer behavior is not physically tested. Credentials are not persisted, though ordinary managed-runtime strings cannot promise physical memory zeroization.

One GATT client and one pending operation are retained. Service UUID, discovered device IDs and all four read/write characteristics are checked. Native callbacks use connection generations; cancellation, timeout, backgrounding and failure close the link. Unknown cleanup failure blocks reuse until an app restart. Request MTU is 517; the current firmware's preferred MTU is 256. The app submits whole attributes of at most 512 bytes using `WRITE_TYPE_DEFAULT`, then waits for the write-completion callback before reading. Android's standard ATT prepare/execute and long-read implementation handles smaller negotiated MTUs. This app implements no protocol fragmentation, truncation, pairing or automatic reconnect.

The two shared modules are exact copies from baseline `a3003665f0a0a92f8bc66b470f662cebb97756eb`; see [shared-provenance.json](shared-provenance.json). Node tests use public synthetic vectors against the bundled Security2 implementation. Native tests use injected drivers, not real Android radio hardware.

## Acceptance still required

APK compilation, signing, source tests and static lint do not establish a working phone installation. Actual Android permission prompts, local WebView/crypto loading, negotiated MTU/long attributes, correct/wrong personal login, history, Done/background disconnect and backup resumption need physical acceptance. No phone/device/firmware/cloud action has been performed in this lane.

Official references: [local HTTPS assets](https://developer.android.com/develop/ui/views/layout/webapps/load-local-content), [restricted WebView message listener](https://developer.android.com/reference/androidx/webkit/WebViewCompat#addWebMessageListener(android.webkit.WebView,java.lang.String,java.util.Set,androidx.webkit.WebViewCompat.WebMessageListener)), [Nearby devices permissions](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions), [GATT write/MTU API](https://developer.android.com/reference/android/bluetooth/BluetoothGatt), [Android 16 standard long attributes](https://android.googlesource.com/platform/packages/modules/Bluetooth/+/refs/tags/android-16.0.0_r1/system/stack/gatt/gatt_cl.cc), [AGP compatibility](https://developer.android.com/build/releases/agp-8-13-0-release-notes), [WebKit releases](https://developer.android.com/jetpack/androidx/releases/webkit), [APK signing](https://developer.android.com/studio/publish/app-signing).
