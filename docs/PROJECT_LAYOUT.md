# Project layout

This repository contains three cooperating applications: the dongle firmware,
the desktop setup application and the Android nearby viewer. The original
LILYGO T-Dongle S3 is the embedded target. Other boards need an explicit port;
similar names do not establish compatible pins, memory or flash layouts.

## Top-level map

| Path | Purpose |
| --- | --- |
| `platformio.ini`, `boards/`, `partitions.csv`, `sdkconfig.defaults` | Firmware profiles, board definition, flash layout and SDK defaults |
| `src/`, `include/` | Firmware implementations and bounded shared models/interfaces |
| `lib/tinyxml2/` | Retained XML parser source and its license notice |
| `sdk/` | Pinned upstream SDK inputs, reviewed patches and provenance |
| `desktop/` | Python/Qt desktop application, local frontend, packaging and tests |
| `android/` | Native Android BLE bridge, bundled viewer assets, Gradle build and tests |
| `phone/` | Browser setup clients, protocol reference tests and historical previews |
| `test/`, `tools/` | Firmware/native/UI fixtures, source gates, profile verification and release tools |
| `docs/` | Developer navigation, design notes, focused reviews and supporting guides |
| `artifacts/` | Version-specific public build/test evidence; availability depends on the release package |
| `data/` | Firmware filesystem inputs; changes can affect a generated filesystem image |

Private device backups and signing material do not belong in this tree or a
public source archive. Generated directories and older evidence are not a
substitute for the current source and release manifest.

## Firmware boundaries

```text
main.cpp / wifi_setup_runtime.cpp
  ├─ USB host path       usb_storage.cpp → block I/O + snapshot generation
  ├─ backup worker       sync_engine.cpp → pro_core.cpp → webdav_client.cpp
  │                                         └─ pro_journal.cpp → receipts
  ├─ setup / ownership   ble_management.cpp + owner/Wi-Fi/cloud record stores
  ├─ network assistance  Wi-Fi radio/scan + public Wi-Fi probe/owned route
  └─ presentation        lcd.cpp + local_dashboard.cpp / assets / model
```

`include/pro_snapshot.h` and `include/pro_blockio.h` describe the read-session
and physical-transfer boundaries. `include/pro_core.h`, `include/pro_transfer.h`
and `include/pro_device.h` carry bounded backup state. `include/scan_idle_gate.h`
controls no-change scan reuse; any additional scan-cache optimization must retain
the host-write invalidation contract.

`src/ble_sdk_*` supplies guarded integration with the pinned Espressif transport,
Security 2 and host/controller lifecycle. The original and patched inputs live in
`sdk/pinned_ble/` and `sdk/pinned_nimble_stop/`; the verifier tools check provenance
and actual linked providers. `sdk/pinned_http_upload/` retains the original
HTTPClient input used by the optional reviewed upload adaptation.

Persistent login, Wi-Fi and cloud records have separate store modules. File
history and event summaries have separate bounded interfaces. The local LAN
dashboard exposes status; private authenticated history is a different interface.
Configured Wi-Fi or cloud settings do not by themselves prove Internet access
or a successful cloud backup.

## Desktop application

| Path | Responsibility |
| --- | --- |
| `desktop/cloudstick_desktop/main.py` | Native window, bundled resource policy, bridge dispatch and smoke mode |
| `protocol.py`, `transports.py` | Strict operations, native BLE/USB transport and job ownership |
| `setup_access.py`, `enrollment.py` | Device-bound remembered access and physically gated first-owner enrollment |
| `diagnostics.py`, `release_review.py` | Typed read-only health snapshot and release metadata review |
| `desktop/frontend/bootstrap.mjs`, `bridge.mjs` | Trusted local startup and bounded native request adapter |
| `wizard-model.mjs`, `wizard.mjs` | Step state and visible Next/Back flow |
| `app.mjs`, `runtime.mjs` | Existing validated setup actions and sanitized state publication |
| `direct-setup.mjs`, `first-owner.mjs`, `service.mjs` | Focused onboarding and service-screen coordinators |
| `desktop/frontend/shared/` | Frozen reference assets with provenance; the active desktop entry is the parent `index.html` |
| `desktop/packaging/`, `desktop/tools/` | Linux/Windows bundle recipes, notices, validation and install metadata |

The desktop release currently reviews firmware bundles; that is not an enabled
hardware update writer. Stock installation, recovery and app-only update support
must be described by the exact release's supported operations, not inferred from
a service-screen title. See [desktop instructions](../desktop/README.md).

## Android and browser clients

The Android viewer starts in `MainActivity.java`. `StrictRequest.java` and
`LocalPolicy.java` restrict the local WebView bridge. `PermissionGate.java`,
`GattSession.java` and `AndroidBleDriver.java` handle permission ordering, GATT
ownership and cleanup. `android/app/src/main/assets/` contains the local UI and
shared Security 2/connection modules. It is a nearby read-only viewer, not an
Internet relay, setup editor or firmware updater. See its
[guide](../android/README.md) and [notices](../android/THIRD_PARTY_NOTICES.md).

`phone/wifi-setup/` contains the browser setup client. `phone/milestone1/` retains
the earlier protocol reference and tests; `phone/wifi-preview/` is a synthetic
preview. Their names identify scope and history, not interchangeable production
entry points. Desktop and Android use their bundled entry points and do not need
a hosted setup website.

## Reports and evidence

Start at the [public project history](PROJECT_HISTORY.md) for ordered source
identities and scope, then the [release guide](RELEASES.md) and dated public
report for artifact hashes and current acceptance. Raw operator/recovery
dossiers and machine history remain private; they are not part of the public
source export. Historical failures are summarized honestly in the curated index.

The [developer guide](DEVELOPER_GUIDE.md) maps changes to tests. The
[credits](CREDITS.md) distinguish inherited implementation, dependencies and
research references, including the unresolved upstream license-text gap.
