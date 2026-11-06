# Desktop setup 0.1.0 — implementation and validation

Prepared 2026-11-05. This is the first local desktop application, based on frozen
Cloud Stick Pro source `6bb5082a36b84b8451875d0fa7201f0d95192e52` (`0.4.0-rc5`).
Development branch: `desktop/setup-app`. The existing product release remains
unchanged. No physical device, real account, private key, SD card or hosted site
was contacted or changed during this application build.

## Delivered behavior

The PySide6 application bundles its interface locally at `cloudstick://app`;
there is no HTTP listener or dependency on a ChatGPT account/setup website. The
existing Security2 protocol clients and five original phone assets are preserved
byte for byte under `frontend/shared`. A small desktop wrapper injects native
Bluetooth through a constrained QWebChannel interface. Bleak uses the Windows or
Linux OS backend; browser Web Bluetooth is not required.

The interface includes existing-owner key import/sign-in, personal-login
replacement, Wi-Fi discovery/check/staged save, Koofr staged settings, recorded
backup facts, authenticated file history/activity and venue sign-in guidance.
These reuse the existing firmware capabilities and keep their explicit commit,
uncertain-save, authentication and teardown barriers. A saved cloud setting still
does not prove account access or a verified backup.

USB lists only matching Espressif native CDC ports with a stable identity. After
an explicit click it can read sanitized `STATUS` or request `BENCH_WINDOW`; it
cannot send arbitrary commands, reset or flash a device. Native port ownership is
retained until a cancelled operation actually finishes. Settings use Bluetooth,
not a new USB settings transport. A requested setup window is not evidence that
the radio is ready or that authentication succeeded.

The local release-manifest picker reads bounded JSON and shows selected public
identity fields. **It cannot install app or firmware updates.** A signature claim
inside an untrusted manifest never enables installation. There is no updater
endpoint, signing key, firmware writer or new factory-enrollment path in 0.1.0.

## Implementation choices

The earlier plan's Tauri choice was provisional. This implementation uses
PySide6/QtWebEngine so the existing JavaScript clients can be retained with a
Python native transport and a reproducible native packaging path. Qt/Chromium
makes the package considerably larger than the firmware; it runs on the computer
and does not consume additional dongle flash/RAM.

The Qt origin is registered as secure before QApplication starts. Only bundled
assets and the installed Qt WebChannel script can load. Remote/file/foreign-origin
resources are blocked. External same-window links require confirmation before
opening the normal browser. The page uses an off-record profile, memory cache and
no persistent cookies. This is not proof of operating-system swap/crash-dump/RAM
erasure. The app does not save credentials or implement Remember me.

Closing tears down the native session and worker before destroying the page and
profile. The native boundary accepts a bounded operation/argument allowlist and
monotonic request IDs. It forwards fixed error codes rather than OS exceptions,
stack traces or raw GATT payloads. A timed-out write is never retried automatically.
Unknown cleanup retains a blocked transport state instead of starting a new link.

## Tests actually executed

| Check | Result | Scope |
| --- | --- | --- |
| Python unit tests with `-W error` | 61 PASS | Transport mocks, late/cancelled operations, USB port identity/line sequence, shell boundaries, bounded manifest reads and shutdown races |
| Desktop frontend Node tests | 15 PASS | Selection, RPC validation, late replies, disconnection, strict browser timer receiver and unique DOM IDs |
| Existing shared setup regressions | 470 PASS, 0 skipped | Login/crypto, cloud, history, public Wi-Fi, activity and backup admission; one preserved pinned SDK synthetic host is explicitly selected |
| Pinned Python dependencies | PASS | `pip check` and exact build-input checks |
| Actual Qt source smoke | PASS | Local secure origin, WebCrypto, production SRP start, AES-GCM round trip, native synthetic request/reply, login enabled and off-record profile |
| Linux native package | PASS | PyInstaller onedir plus portable archive and `.deb`; actual final executable smoke also passes |
| Firmware/source preservation | PASS | No tracked changes to firmware, phone clients, partitions, SDK, build flags or existing tests |

Machine results and command/log hashes are in
`../artifacts/desktop-setup/test-results.json`, `source-smoke.json` and
`packaged-smoke.json`. The SDK host
was reused from the frozen RC5 synthetic bench and hashed, not rebuilt or flashed.
This does not extend its original coverage into physical Bluetooth acceptance.

Initial failures remain documented: a build-input setuptools constraint required
the compatible pinned 80.9.0; the minimal container needed locally extracted Qt
runtime libraries; headless GPU initialization needed the software test backend;
the first regression commands omitted the SDK host and then its native library
path. Those commands failed and were rerun with their proper prerequisites.
The actual Qt test also found a duplicate DOM ID and incorrectly bound browser
timers that Node had tolerated. Both were fixed with regressions. The smoke-only
relative dynamic import was corrected to the explicit local origin. No sandbox,
TLS, watchdog or sanitizer protection was disabled.

## First use on the current board

1. Open the Linux app. Find/select the USB device and read status. If another
   serial tool owns it, close that tool before retrying; the app must not compete.
2. Request **Open Bluetooth setup**, or use the existing BOOT procedure when
   needed. Enter the current login or choose the existing private key file.
3. Click **Find Cloud Stick**, select the expected stick and authenticate. All
   settings continue to require the existing private owner authentication.
4. Use Wi-Fi **Find**, select a network, **Check**, review and explicitly **Save**.
   Koofr settings and personal-login replacement have their own review/commit steps.
   A personal password must meet the existing protocol's rules.
5. Disconnect in the app and leave the dongle powered. Backups remain the dongle's
   job after setup safely closes. The short setup window can also expire.

## Outstanding acceptance and features

Actual Linux Bluetooth/USB operation through this application, private key file
import through the real native chooser, chosen-password migration, real Wi-Fi/
cloud saves and simultaneous host storage behavior remain untested in this task.
Windows source/build recipes exist, but no Windows executable/installer was built
or run here. Linux offscreen testing does not validate desktop graphics drivers,
BlueZ permissions or Windows GATT behavior.

Secure USB-only configuration, authenticated application updates, guarded firmware
installation, forgotten-login recovery and first-owner enrollment for a new board
remain future work. This application does not resolve the product's existing
venue, workstation, receipt-capacity, larger-file or endurance acceptance gates.
The ordinary phone website remains unchanged as a fallback. No neutral hostname
was published, and no GitHub push was performed.

See [README](README.md) for installation/build commands and
[packaging notices](packaging/THIRD_PARTY_NOTICES.md) for dependencies and the
remaining public-redistribution attribution work.
# Desktop 0.5.0 service workspace

Adds an explicit read-only USB health screen and safe diagnostic export, retaining
the first-account/personal-login wizard. The fixed native diagnostic command set
is STATUS, BOOT_STATUS, MEMORY_STATUS, BACKUP_STATUS and DASHBOARD_STATUS.
Requests use the existing exclusive nonreset serial session, fresh USB identity
checks and 350 ms complete-response pacing. Exact schemas reject private/unknown
fields; 64-bit transfer counters remain decimal strings across the bridge.
Cancellation, unknown cleanup and active/staged setup retain their ownership
barriers. No firmware, partition, SD or receipt change is introduced.

The source suite passes 148 Python and 112 frontend checks (260 total). An actual
current-owned read-only observation passed with unchanged USB epoch/revision,
Wi-Fi connected, backups unpaused, ten recorded verified/zero pending/failed and
no worker error. This is not a fresh cloud or file-integrity check. The preserved
first-owner positive hardware, factory, Windows and Android gates remain separate.

The service screen reviews local release metadata only. It does not authenticate
its own manifest assertions, enable a firmware writer, perform a recovery restore
or execute downloaded code. A separate synthetic firmware-service engine is not
runtime integration. App upgrades use the existing full versioned package.

Earlier implementation report follows for historical context.
