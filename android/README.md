# Cloud Stick Viewer for Android

Android 12+ companion for an already configured Cloud Stick Pro. **0.2.0 opens
with a live Wi-Fi dashboard.** You do not need to sign in or open Bluetooth to
check ordinary device status. The stick continues its normal backup work.

## Everyday use

1. Keep your phone and stick on the same reachable Wi-Fi or phone hotspot.
2. Open **Cloud Stick Viewer**. Enter the IP address shown on your stick—the same
   one that works in your phone’s browser—and tap **Save & connect**.
3. The app remembers that address on this phone. Next time, open the app to see
   current activity, backup counts, transfer progress, storage and Wi-Fi status.
4. If the address changes after switching networks, open **Stick connection** and
   replace it. **Forget address** removes the saved address.

The dashboard refreshes while it is visible. Losing the connection clearly marks
previous readings as old and hides transfer progress. Backgrounding the app stops
polling; returning reads again. There is no background monitoring or automatic
network scan. Some guest networks block connections between devices.

Only the IP address is remembered. Passwords and results are not saved. The local
Wi-Fi endpoint provides **public, unauthenticated status**, like the existing
browser dashboard. It is not private file history or an independent cloud check.
No login or owner key is sent to it.

## Private file history

1. Choose **Private history**. Hold the stick’s BOOT button for 3 seconds and release.
2. Enter your personal Cloud Stick login, tap **Sign in & find my stick**, allow
   Nearby devices when Android asks, and choose your own advertised stick.
3. Read the recorded backup status and up to eight saved file summaries.
4. Tap **Done — Close Bluetooth**, or return to **Live dashboard**. Returning waits
   for the native Bluetooth close acknowledgement; a failed close keeps the
   private view open and does not claim backups resumed.

Bluetooth management pauses the backup worker. The Wi-Fi dashboard is stopped
while viewing private history. Backgrounding clears private results and closes
Bluetooth; a fresh login is required next time. Recorded results are not a live
file listing or proof that a host application finished saving. The firmware’s
inventory remains bounded to 128 entries; this view shows up to eight summaries.
Partial/faulted snapshots stay labelled. Firmware idle/safety limits still apply.
Read-only describes this app’s command choices; the authenticated firmware role
remains the owner role. No settings mutation commands are called.

## Build

Pinned: Java 17, Gradle 8.13, Android Gradle Plugin 8.13.2, compile/target SDK 36, Build Tools 35.0.0, AndroidX WebKit 1.14.0. No Kotlin/Compose compiler toolchain. Set `JAVA_HOME`, `ANDROID_HOME` and `GRADLE_USER_HOME` to your local tools/cache directories, outside this source tree. Native SDK signing tools also need the JDK `bin` directory on `PATH`.

Prepared input hashes: Temurin17.0.21+9 tar SHA256 `8b9b611d917e3dfd8419d3566b5949226f69b052fed7e109c1368add1dfe8711`; Gradle8.13 ZIP SHA256 `20f1b1176237254a6fc204d8434196fa11a4cfb387567519c61556e8710aed78`; Google command-line tools19.0 ZIP SHA256 `7ec965280a073311c339e571cd5de778b9975026cfcbe79f2b1cdcb1e15317ee` (upstream repository SHA1 `5fdcc763663eefb86a5b8879697aa6088b041e70`). JDK/Gradle matched upstream SHA256; command-line tools matched the official repository checksum. See [third-party notices](THIRD_PARTY_NOTICES.md) for runtime/test pins and inherited Security2 limits.

```sh
./gradlew --no-daemon :app:testDebugUnitTest :app:assembleDebug :app:assembleRelease :app:lintRelease
node --test tests/*.test.mjs
```

`assembleRelease` creates an unsigned, non-debuggable APK. The prepared local release APK is signed using a separate app-specific keystore outside Git/source/public artifacts. Its public certificate and APK hashes are recorded in `artifacts/validation/results.json`. Keep that private signing key to sign future updates; losing it prevents a same-identity in-place app update. Debug APKs are tests only. No phone installation or ADB operation is part of these build commands.

## Native boundary

Seven bundled assets are served only at the exact local HTTPS allowlist, through
`WebViewAssetLoader`. Remote pages/scripts, frames, external navigation, popups,
file/content access, downloads, cookies and Web storage remain blocked. The local
page’s CSP keeps `connect-src 'none'`. The main-frame bridge accepts only strict,
bounded operations. Existing GATT operations and authentication are unchanged.

The new native reader permits a fixed, credential-free `GET /status` on port 80
at a manually selected, canonical RFC1918 IPv4 address. It accepts a plain IP or
an `http://IP/` browser paste, never a hostname, public/loopback address, port,
path, query, fragment or credentials. It bypasses proxies, refuses redirects,
compression, non-JSON responses and unknown/duplicate fields, and caps bodies
at 4 KiB with finite connect/read and total deadlines. Only validated public
fields reach the UI; remote HTML/JavaScript is never rendered. Work is bounded to
one worker and one queued task. Cancellation/generations discard stale results.

Android’s INTERNET permission and cleartext capability are now needed for this
existing local HTTP endpoint. This does not give the renderer network access or
add an Internet/cloud API. Cleartext permission is app-wide, so the native fixed
endpoint and unchanged WebView interception/CSP are essential boundaries. No TLS
exception or certificate bypass is added. Android 12–16 is the current tested
build target; newer OS local-network permission behavior needs physical review.

Only the saved IP is stored in private SharedPreferences. Backup/extraction
exclusions, secure screenshots, no credential persistence and quiet native
logging are retained. Managed-runtime strings cannot promise physical memory
zeroization. No bridge requests or responses are logged.

One GATT client and one pending operation are retained. Service UUID, discovered
IDs and all four characteristics are checked. Generations, cancellation, timeout,
backgrounding and failure close the link. Unknown cleanup failure blocks reuse.
MTU and standard ATT long-attribute behavior are unchanged. No pairing or automatic
Bluetooth reconnect is added. The two shared modules remain byte-exact; see
[shared-provenance.json](shared-provenance.json).

## Acceptance still required

APK compilation, signing, source tests and static lint do not establish a working phone installation. Actual Android permission prompts, local WebView/crypto loading, negotiated MTU/long attributes, correct/wrong personal login, history, Done/background disconnect and backup resumption need physical acceptance. This update has not been installed or physically tested on a phone. Local native transport tests use injected HTTP connections; rendered screenshots use public Wi-Fi/BLE fixtures. No dongle or cloud action is part of these checks.

Official references: [local HTTPS assets](https://developer.android.com/develop/ui/views/layout/webapps/load-local-content), [restricted WebView message listener](https://developer.android.com/reference/androidx/webkit/WebViewCompat#addWebMessageListener(android.webkit.WebView,java.lang.String,java.util.Set,androidx.webkit.WebViewCompat.WebMessageListener)), [Nearby devices permissions](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions), [GATT write/MTU API](https://developer.android.com/reference/android/bluetooth/BluetoothGatt), [Android 16 standard long attributes](https://android.googlesource.com/platform/packages/modules/Bluetooth/+/refs/tags/android-16.0.0_r1/system/stack/gatt/gatt_cl.cc), [AGP compatibility](https://developer.android.com/build/releases/agp-8-13-0-release-notes), [WebKit releases](https://developer.android.com/jetpack/androidx/releases/webkit), [APK signing](https://developer.android.com/studio/publish/app-signing).
