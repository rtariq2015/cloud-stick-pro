# Cloud Stick Viewer for Android

**Viewer 0.4.3-dev** is the Android 12+ companion for a configured Cloud Stick Pro.
[Download the preview](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.08-guided-companion) ·
[Companion guide](../docs/COMPANION_APPS.md) · [Exact versions and test limits](../docs/releases/2026-11-08-guided-companion.md).

## Everyday use

1. Keep the phone and stick on the same reachable Wi-Fi or hotspot.
2. Open **Overview → Stick connection → Find my stick automatically**. One
   discovered stick is saved; with several, choose yours. Discovery needs the
   0.4.8 firmware. You can still enter the address shown on the stick and choose
   **Save & connect**.
3. View current activity, backup counts and live transfer progress without a login
   or Bluetooth connection. The stick continues backing up.
4. If you change networks, find the stick again or replace the saved address.
   **Forget address** removes it from this phone.

The overview refreshes while visible. A lost connection marks readings as old
and hides transfer progress. Backgrounding stops polling; returning reads again.
Some guest networks block communication between devices. The local Wi-Fi status
endpoint is public and does not expose private filenames or accept settings writes.
Finding an address does not authenticate the stick.

## Sign in and manage your stick

For the first connection, choose **My stick**, enable the BOOT fallback, hold the
stick's BOOT button for three seconds and release. Enter your personal Cloud Stick
login, allow Nearby devices if Android asks, and choose your own stick. This is
separate from holding BOOT while reconnecting USB for firmware installation.

After signing in, choose **Trust this phone**. On later visits, leave the BOOT
fallback off and sign in over reachable Wi-Fi. A signed request from this phone
opens a short Bluetooth window, then the normal encrypted login is still required.
Changing the stick's login revokes that trust; trusting another phone replaces it.
Use the explicit BOOT fallback if Wi-Fi is unavailable.

The private view has four sections:

- **Overview:** recorded backup status and the current management session.
- **Wi-Fi:** read up to eight saved networks, scan nearby networks, stage changes,
  check the retained connection and explicitly save. Check the Keep choices before
  removing networks; at least one enabled network must remain.
- **Cloud:** review the Koofr destination, stage account/app-password or folder
  changes, then explicitly save. A settings save is not a new cloud upload test.
- **History:** read up to eight saved file summaries and refresh on request.
  They describe recorded checks, rather than a live host-file listing.

Choose **Done — Close Bluetooth** when finished. Bluetooth management pauses
backups and the Wi-Fi dashboard. The app waits for its native close acknowledgement;
a failed close does not claim backups resumed. Backgrounding clears private data
and closes Bluetooth; sign in again next time. Firmware timeouts still apply.
The phone does not create a first owner account or flash USB firmware; use the
computer app for that guided journey.

## App updates

Open **App updates** to check the signed release, download it and choose
**Install update**. The initial check waits until the app is idle and visible.
Android may ask you to allow installation from Cloud Stick. Return to Viewer and
choose Install again, then confirm the operating system's update screen.
Manual installation of the official APK is also available from the release page.

The downloaded APK must match the declared size and hash, application ID, newer
version code and official signing identity. Updating retains the saved address
and trusted-phone key. Your login password is never saved.

## Build

Pinned: Java 17, Gradle 8.13, Android Gradle Plugin 8.13.2, compile/target SDK 36, Build Tools 35.0.0, AndroidX WebKit 1.14.0. No Kotlin/Compose compiler toolchain. Set `JAVA_HOME`, `ANDROID_HOME` and `GRADLE_USER_HOME` to your local tools/cache directories, outside this source tree. Native SDK signing tools also need the JDK `bin` directory on `PATH`.

Prepared input hashes: Temurin17.0.21+9 tar SHA256 `8b9b611d917e3dfd8419d3566b5949226f69b052fed7e109c1368add1dfe8711`; Gradle8.13 ZIP SHA256 `20f1b1176237254a6fc204d8434196fa11a4cfb387567519c61556e8710aed78`; Google command-line tools19.0 ZIP SHA256 `7ec965280a073311c339e571cd5de778b9975026cfcbe79f2b1cdcb1e15317ee` (upstream repository SHA1 `5fdcc763663eefb86a5b8879697aa6088b041e70`). JDK/Gradle matched upstream SHA256; command-line tools matched the official repository checksum. See [third-party notices](THIRD_PARTY_NOTICES.md) for runtime/test pins and inherited Security2 limits.

```sh
./gradlew --no-daemon :app:testDebugUnitTest :app:assembleDebug :app:assembleRelease :app:lintRelease
node --test tests/*.test.mjs
```

`assembleRelease` creates an unsigned, non-debuggable APK. The prepared local release APK is signed using a separate app-specific keystore outside Git/source/public artifacts. Its public certificate and APK hashes are recorded in the [component manifest](../docs/releases/2026-11-08-companion-components.json). Keep that private signing key to sign future updates; losing it prevents a same-identity in-place app update. Debug APKs are tests only. No phone installation or ADB operation is part of these build commands.

## Native boundary

Only the exact bundled HTML/CSS/module allowlist is served through the local
HTTPS WebViewAssetLoader. Remote pages/scripts, frames, navigation, popups,
file/content access, cookies and Web storage stay blocked. The renderer's CSP
keeps direct network access off. The native bridge accepts strict, bounded
operations and retains one GATT client with generation/cancellation checks.
Private settings use the existing owner authentication and negotiated capabilities.
See [shared provenance](shared-provenance.json).

The native status reader uses a credential-free, fixed `GET /status` on port 80
at a canonical RFC1918 IPv4 address. Redirects, proxies, non-JSON/oversized responses
and unknown/duplicate fields are rejected. It renders validated fields only,
never remote HTML. LAN discovery is bounded and stays separate from authentication.
The trusted-phone opening request uses a fresh nonce, full board binding and an
AndroidKeyStore P-256 signature on the separate port-81 endpoint. It sends no
personal login password over local HTTP and does not leave Bluetooth always on.

App updates use the project's fixed HTTPS feed with normal TLS verification and
a pinned catalog-signing key. Android confirms package identity/signing before
handing installation to its normal installer. Release keys are outside source.

The stick address and public pairing metadata are stored privately; the signing
key stays in AndroidKeyStore. No login password or private history is persisted.
Screenshot protection, backup/extraction exclusions and quiet native logging are
retained. Managed strings cannot promise physical memory zeroization.

## What was tested

The exact 0.4.3 APK was installed over the official-signer app without clearing
its data, then pulled back and hash-checked. A fresh UI test stopped at the phone
lock screen before assertions or authentication. The private helper and temporary
login input were removed; this is not a passing 0.4.3 phone round trip.

The preceding 0.4.2 APK with exact 0.4.8 firmware passed 30 actual-phone assertions:
LAN discovery, three trusted-phone signed openings and normal encrypted sign-ins,
private history/Refresh, saved Wi-Fi reads/scanning and Done. Independent USB checks
confirmed Wi-Fi/backups/memory recovery with unchanged counts and no reset.
The older intermittent firmware memory-recovery fault remains open in issue #5.
These passing results belong to the recorded APK, not every later app version.

Local software validation for this preview passed 241 companion JavaScript tests,
63 JVM tests, release build/lint and 60 actual-asset synthetic render checks.
The [version report](../docs/releases/2026-11-08-guided-companion.md) records exact
hashes and separates earlier emulator/phone evidence. Settings writes, actual
access-point outage, password revocation and endurance still need physical checks.

Official references: [local HTTPS assets](https://developer.android.com/develop/ui/views/layout/webapps/load-local-content), [Nearby devices permissions](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions), [APK signing](https://developer.android.com/studio/publish/app-signing).
