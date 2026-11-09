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

The exact-source target build and protected installation have passed. Three
actual native Bluetooth sessions are recorded below; guest association and venue
approval have not passed.
No speed improvement, new verified cloud receipt or guest sign-in is claimed.
Private hardware evidence and recovery copies are excluded from publication.
The source/binary publication gate remains in [Credits](../CREDITS.md).

## Exact build evidence

The final candidate was built from source
`c61791697a81e04a3738533b05dcd70e475f3de9`.
Application: **1,704,752 bytes**, SHA-256
`da3237ffa797ff2ac89d7e2cc6c5666ebcf502896962b669a0f312479e253870`.
All 6,509 frozen SDK entries matched before and after; partition bytes and the
existing USB/storage review gates passed. The fixed USB command returns one
bounded JSON reply containing both numeric records.

Compared with the retained installed 0.4.10 build, static RAM rose from 84,804
to **84,860 bytes** (+56), and the application binary grew by **640 bytes**.
These build measurements do not replace the runtime contiguous-memory proof.
A private signed installation package passed verification under the actual
no-swap/core-dump-disabled scope. It was subsequently installed as described
below. It has not entered the normal catalog/cache or public downloads.

## Physical follow-up: connection still fails, cleanup fault reproduced

The owner performed the required BOOT reconnect. Two fresh matching encrypted
16 MiB recovery reads, reopened verification, application-only installation,
full preservation/readback and saved owner/revision checks passed. The actual
worker had swap maximum/current both zero and core dumps disabled. The installed
application matches the exact 0.4.11 hash above. A transient pyserial enumeration
failure occurred before any worker/write; retrying after USB settled used the
same physical ROM session. The separate desktop fix is in
[PR 29](https://github.com/rtariq2015/cloud-stick-pro/pull/29).

Three real Linux native-transport sessions used normal Security 2 and matched
the complete wired peer identity. These are not packaged desktop GUI tests:

| Check | Actual result |
| --- | --- |
| First guest scan/check | Guest absent from stick scan; check failed after 6,118 ms. Retained disconnect reason 36 (station leaving), no address flag. No Save. |
| Repeat without reboot | Guest absent again; check failed after 6,299 ms. Retained reason 201 (no AP found), no address flag. No Save. |
| Saved-hotspot control | Hotspot visible; connection check passed after 9,583 ms with a usable local address while Bluetooth was active. Used the existing saved secret, then Abort; no Save. |

Both guest-session closes restored memory and the prior local connection,
resumed backups and did not reset the stick. The control preserved the two
saved networks and their revision. It then reproduced the existing cleanup
failure: largest block **118,772 → 63,476 bytes**, despite **219,916 bytes** total
free. NVS release reported step 8, SDK error zero and flash equality. Bluetooth
was off and the backup worker stopped; there was no store fault. Three verified,
six pending and zero failed counters were retained. This is not a new verified
upload or evidence that the six local test files were erased.

A later sequential snapshot, **182,365 ms after the failed-close snapshot**,
showed the largest block back at **122,868 bytes**, with total free up by 32
bytes. The firmware remained blocked because its 30-second closing deadline
had already expired. This is evidence of delayed memory recovery, not proof of
the remaining allocation's caller or lifetime. The owner then performed a
normal power reconnect; lower observed uptime, owner ready, Bluetooth off,
Wi-Fi locally connected and backups unpaused confirmed recovery. No safeguard
was overridden. [Issue 5](https://github.com/rtariq2015/cloud-stick-pro/issues/5)
remains open.

Fresh laptop scans varied: one showed a weak 2.4 GHz guest entry as well as
5 GHz entries, another found none. The venue cannot be described as permanently
5 GHz-only. The stick's ESP32-S3 supports 2.4 GHz, but the exact cause of its
failure to detect this guest AP remains unverified. Reason 36 alone does not
establish that retry handling caused the failure; the repeat reported 201.

A separately requested staff network advertises enterprise authentication.
No existing workplace profile or official certificate/server settings were
available. This firmware supports open/personal networks, not enterprise
profiles; no staff-credential authentication attempt was made. Credentials,
network identities, addresses and raw logs are excluded from these notes.

## Primary references

- [Espressif ESP32-S3 radio capabilities](https://www.espressif.com/en/products/socs/esp32-s3)
- [Espressif IDF 5.5.5 Wi-Fi API and scan defaults](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/network/esp_wifi.html)
- [Espressif Wi-Fi FAQ: scan and beacon timing](https://github.com/espressif/esp-faq/blob/master/docs/en/software-framework/wifi.rst)
- [Espressif coexistence guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32s3/api-guides/coexist.html)

The superficially similar [IDF issue 17871](https://github.com/espressif/esp-idf/issues/17871)
concerns IDF 5.2.6 and Android hotspots. It is not proof of this work-network
failure or a reason to replace the frozen framework blindly.
