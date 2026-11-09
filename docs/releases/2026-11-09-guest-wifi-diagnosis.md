# Guest Wi-Fi diagnosis — 9 November 2026

The work guest network has **not** been added. The last direct connection check
failed before Save, so the two existing saved networks were retained. Reaching a
network from a laptop does not yet establish that the stick can join it.

## What was actually observed

The installed private firmware is still **0.4.10-upload-dev**, application SHA-256
`d6938a83200ac4459ccc6723e3d15c5a5a272aa4fea14364920b888280037df5`,
source `e4d6987dddb13026350be1141455ed589b83ad25`. Its last direct check returned
Unavailable after 7,108 ms. A previous attempt returned an address-related error.
Neither attempt committed the candidate. Normal authenticated Bluetooth cleanup
restored memory and the prior local connection without a reset or store fault.
These results do not prove the cause, a successful guest login, or cloud access.
The earlier intermittent Bluetooth cleanup fault remains a separate open issue.

## Research and the small candidate change

The retained framework header identifies ESP-IDF **5.5.5**. Its retained build
configuration enables software coexistence; Wi-Fi and both Bluetooth tasks are
pinned to core 0. No framework upgrade, core reassignment or radio preference
change has been made on that evidence alone.

Two application-level problems can be addressed without replacing the SDK:

- The setup scan used a 30 ms minimum and 80 ms maximum per active channel.
  Espressif documents a default of **0/120 ms**, and recommends enough scan time
  to cover a typical beacon interval. The candidate restores those defaults.
  A longer setup scan can improve discovery; it does not prove that direct
  connection failures are resolved.
- Failure cleanup overwrites the live SDK step and consumes the disconnect
  reason. The candidate retains the completed attempt and last failed attempt
  as numeric metadata, including elapsed time, SDK result, reason and observed
  connection/address-validation flags. Teardown and fallback cannot erase that
  last failure. Reports contain no network name, address, password or payload.

These changes belong to the private **0.4.11-wifi-diag** candidate in the existing
upload-development environment. Historical firmware environments keep their
original behavior. Both new feature flags must be explicitly admitted by the
build guard; the fixed `WIFI_ATTEMPT_STATUS` USB report is absent elsewhere.
The normal signed update feed and published firmware downloads have not changed.

The retained Arduino core also has a one-time first-connection retry even with
its ordinary auto-reconnect switch off. That is a potential interaction to
investigate, not a demonstrated root cause. It has not been patched or bypassed.

## Checks and limits

Actual radio source passed sanitizer tests with simulated SDK events, DHCP and
radio conditions: 131 historical, 165 release-path and 200 diagnostic assertions.
The diagnostic cases cover a beacon-only AP arriving after the old scan window,
retained disconnect reason, SDK failure before cleanup, invalid gateway, lost
address, successful fallback, cancellation, radio reinitialization and clock wrap.
All 62 build-profile self-tests passed. The full native integration runner also
passed, including normal and failure-path enrollment plus compilation of the
pinned Security 2 runtime. None of these are physical RF acceptance.

A fresh exact-source firmware build, protected installation, actual guest
association/address test and any required browser acceptance remain separate.
No speed improvement, new verified cloud receipt or guest sign-in is claimed.
Private hardware evidence and recovery copies are excluded from publication.
The source/binary publication gate remains in [Credits](../CREDITS.md).

## Primary references

- [Espressif IDF 5.5.5 Wi-Fi API and scan defaults](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/network/esp_wifi.html)
- [Espressif Wi-Fi FAQ: scan and beacon timing](https://github.com/espressif/esp-faq/blob/master/docs/en/software-framework/wifi.rst)
- [Espressif coexistence guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-guides/coexist.html)

The superficially similar [IDF issue 17871](https://github.com/espressif/esp-idf/issues/17871)
concerns IDF 5.2.6 and Android hotspots. It is not proof of this work-network
failure or a reason to replace the frozen framework blindly.
