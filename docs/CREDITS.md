# Credits and source provenance

Cloud Stick Pro derives from **Cloud Stick by John / JMcrafter26**, rather than an
independent recreation of the original idea. The retained upstream baseline is
[`27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d`](https://github.com/JMcrafter26/cloud-stick/tree/27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d).
Its ESP32-S3 USB, WebDAV, display and configuration work formed the starting
project. Pro changes the backup/storage model and adds its own setup, verification,
diagnostics and companion interfaces. This is a derivative project, not an
official release or endorsement from the upstream author or hardware vendor.

The original description and acknowledgements remain in
[README.upstream.md](../README.upstream.md). See also the existing
[upstream notice](../UPSTREAM_NOTICE.md).

## Upstream license evidence

Verified against the official GitHub repository on **2026-11-06**:

| Official source | Observation |
| --- | --- |
| [Pinned upstream README](https://github.com/JMcrafter26/cloud-stick/blob/27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d/README.md#license--acknowledgements) | Describes the project as MIT-licensed and links a `LICENSE` file |
| [Current upstream main commit](https://api.github.com/repos/JMcrafter26/cloud-stick/commits/main) | Resolves to the same full baseline commit above |
| [Complete recursive tree](https://api.github.com/repos/JMcrafter26/cloud-stick/git/trees/27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d?recursive=1) | Reports 29 entries, `truncated: false`; no LICENSE, COPYING or NOTICE path |
| [GitHub license endpoint](https://api.github.com/repos/JMcrafter26/cloud-stick/license) | Returns HTTP 404 |
| [Pinned LICENSE URL](https://raw.githubusercontent.com/JMcrafter26/cloud-stick/27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d/LICENSE) | Returns HTTP 404; the `main/LICENSE` URL also returns 404 |

The README assertion is preserved, but the corresponding grant/copyright text
was not available at those URLs. This document supplies attribution and evidence;
it does not invent an upstream MIT grant, assign copyright or declare the
licensing record complete. The development source is now published with this
unresolved evidence clearly retained. A surrounding development workspace's
license must not be substituted for it. Preserve the original assertion and
all applicable component notices; publication does not resolve this gap.

## Hardware, embedded libraries and retained artwork

| Project / maintainers | Contribution and retained evidence |
| --- | --- |
| [LILYGO / Xinyuan-LilyGO T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3) | Original board, pinout, schematics and USB/display/SD examples; upstream also identifies these as a USB reference |
| [Espressif Arduino-ESP32](https://github.com/espressif/arduino-esp32), [ESP-IDF](https://github.com/espressif/esp-idf) and [Apache NimBLE](https://github.com/apache/mynewt-nimble) | Embedded framework, radio stack, protocomm/Security 2 and task integration; pinned source, patches and notices are retained under `sdk/` |
| [pioarduino](https://github.com/pioarduino/platform-espressif32) and [PlatformIO](https://platformio.org/) | Pinned firmware platform and build tooling; exact project pins are in `platformio.ini` |
| [TinyUSB](https://github.com/hathach/tinyusb) | USB stack used through the ESP32 Arduino framework |
| [Bodmer / TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | Display library and existing glyph/font resources, pinned at `2.5.43` |
| [Benoît Blanchon / ArduinoJson](https://github.com/bblanchon/ArduinoJson) | JSON support, pinned at `7.4.3` |
| [TinyXML-2](https://github.com/leethomason/tinyxml2) | Retained XML parser; keep `lib/tinyxml2/LICENSE.txt` and source notices |
| [sbrin / Lopaka](https://github.com/sbrin/lopaka) | Upstream credits the dashboard icons/design to Lopaka; inherited bitmap arrays remain in `src/lcd.cpp` |

The inherited artwork attribution is retained as upstream supplied it; no new
copyright owner or artwork license is inferred from that acknowledgement.
Original FastLED use is historical: upstream credits
[FastLED](https://github.com/FastLED/FastLED), while the current firmware's declared
direct library dependencies are TFT_eSPI and ArduinoJson. Do not describe FastLED
as an active Pro dependency without checking the selected build.

The Security 2 JavaScript implementation derives from the official Espressif
provisioning reference. Retained Espressif headers, Apache-2.0 text, protocol
provenance and implementation limitations accompany the source under
`phone/milestone1/`, `phone/wifi-setup/`, `sdk/pinned_ble/` and the companion
applications. Reviewed local SDK adaptations preserve their upstream sources
and explicit patches rather than claiming unmodified upstream behavior.

## Desktop and Android components

The desktop application uses CPython, Qt/PySide6 and Qt WebEngine, Bleak,
pyserial and the relevant Linux D-Bus or Windows WinRT backend. PyInstaller
packages the native release. Pins are in `desktop/requirements.txt`;
[desktop third-party notices](../desktop/packaging/THIRD_PARTY_NOTICES.md) and
the accompanying `licenses/` directory identify the applicable component terms,
including Qt/Chromium notices. Preserve those files with binary bundles.

The 0.6.0 preview aggregates an independently built esptool-based installer
beside the Qt GUI. Its new engine/worker/USB modules carry GPL-3.0-or-later
identifiers; five new shared firmware-support modules carry a scoped MIT grant.
The helper ships its preferred Python source and esptool's original notice and
source in `installer-source.zip`. This does not relicense inherited firmware,
artwork, existing frontend or any other component, and does not resolve the
upstream license-text gap. Keep the separate firmware data/source identities,
component notices and source/binary review gate. No signing key or device
checkpoint is a public release input.

Android Viewer 0.2.0 adds an independent public-LAN dashboard reader and UI,
with scoped MIT notices and the exact grant in its third-party notices. It keeps
the inherited authentication/protocol sources and notices unchanged. The local
HTTP dashboard does not carry login credentials or replace authenticated history.

The Android viewer uses Android's BLE/WebView APIs and AndroidX WebKit, with its
resolved dependencies, Gradle/JDK build tools and JUnit tests listed in
[Android third-party notices](../android/THIRD_PARTY_NOTICES.md).
`android/shared-provenance.json` records the reused protocol assets. Signing keys
are private release inputs, not source or attribution material.

## Research references versus copied implementation

[WebStick by tobychui](https://github.com/tobychui/WebStick) was reviewed for SD
file-server and interface ideas. It was not used as a replacement firmware or
copied into the reference-review update; see [REFERENCE_REVIEW.md](REFERENCE_REVIEW.md).
Its host/storage architecture is distinct from this project's USB MSC boundary.
The LILYGO archive and vendor examples supplied hardware/reference information;
reviewing another example does not establish that its code was copied or that it
solves concurrent host filesystem access.

WebDAV behavior is informed by [RFC 4918](https://www.rfc-editor.org/rfc/rfc4918).
Standards and research links explain design inputs; they are not additional
software dependencies or a substitute for implementation provenance.

Keep existing author headers, component licenses, original source snapshots and
review patches when moving or packaging files. Record new copied material at the
time it is introduced, including its exact origin and applicable notice.
