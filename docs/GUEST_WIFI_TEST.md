# Test workplace guest Wi-Fi without uploading files

This optional firmware mode checks whether the original LILYGO T-Dongle S3 can
use **Guest-Net** directly. It adds to the existing Cloud Stick firmware; it is
not a replacement example or a Raspberry Pi service. **It has not yet proved a
connection to the actual workplace network. No device was flashed for this task.**

The owner's laptop reportedly connected with `nmcli device wifi connect
"Guest-Net"`, received HTTP204 and HTTPS200, and needed no sign-in page.
Those are laptop results. The stick needs its own scan, connection and internet
test. The S3 uses 2.4 GHz; a successful laptop connection does not establish that
the same access point is reachable by the stick.

## What this mode does

1. Wait for an explicit serial `GUEST_TEST_START` command.
2. Scan specifically for Guest-Net on the S3's 2.4 GHz radio. Filtering prevents
   unrelated stronger networks from filling the existing 20-record result limit.
3. Report the actual security type and RSSI. Connect only if every retained
   matching result is open and the scan is complete. Protected, conflicting,
   unknown or truncated results stop the test without trying a password.
4. Use the existing RAM-only Wi-Fi adapter and DHCP validation. Try at most twice,
   with the existing 25-second connection/DHCP timeout for each attempt.
5. Resolve `connectivitycheck.gstatic.com` and `www.google.com` using the supplied
   DNS service. Send fixed, credential-free GET requests to:
   - `http://connectivitycheck.gstatic.com/generate_204`
   - `https://www.google.com/`
6. Report HTTP status, HTTPS status/verification, DNS results, clock state and
   connection errors. HTTP204 plus verified HTTPS200 means **Internet Available**.
   A redirect or other unexpected positive HTTP response means **possible captive
   portal**. It is a hint, not proof of a portal; server errors can also cause it.

No redirect is followed and no terms, forms, passwords or access controls are
submitted. Responses stop at the end of the headers; no response body is read by
the diagnostic. Header reading has a five-second deadline and 4096-byte budget.
The first status line is limited to95 bytes. TCP connection and TLS handshake
timeouts are five seconds each. The pinned lwIP resolver has its own finite
retries, which can take longer than the socket timeout. Cancellation lets a live
resolver/transport call return and reclaims the task normally; it never forcibly
deletes a running task or resets USB.

HTTPS uses the frozen SDK's Mozilla CA bundle and the actual `www.google.com`
hostname. Certificate dates and hostname validation stay enabled. The existing
SNTP service must obtain a usable clock; this mode waits up to20 seconds. Before
cloud setup, public fallback time servers are used in RAM. If time or certificate
validation fails, HTTPS remains unverified. It never retries insecurely.

## Existing features and boundaries

The project uses **PlatformIO with Arduino3.3.12 on ESP-IDF5.5.5**, pinned through
pioarduino55.03.312. Existing responsibilities remain:

| Feature | Existing implementation |
| --- | --- |
| Managed Wi-Fi, saved profiles, DHCP, scan and reconnect | `src/wifi_setup_radio.cpp`, `src/wifi_setup_runtime.cpp`, `lib/wifi_setup_model/` |
| Authenticated BLE provisioning and owner management | `src/ble_management.cpp`, owner/login stores and the existing runtime |
| USB mass storage and microSD ownership | `src/usb_storage.cpp`, block I/O/snapshot interfaces |
| Files, receipts and cloud uploads | `src/sync_engine.cpp`, `src/pro_core.cpp`, `src/pro_journal.cpp`, `src/webdav_client.cpp` |
| Local web status and discovery | `src/local_dashboard.cpp` |
| Tiny LCD | `src/lcd.cpp`, existing owner-loop renderer |
| New optional diagnostic | `src/guest_diagnostic.cpp`, `src/guest_internet_probe.cpp` |

The diagnostic image blocks the cloud worker from startup, including after test
completion, cancellation and BLE setup. There is no startup upload window. It
does not enumerate user files, acquire a storage read view, change cloud settings,
save Guest-Net, alter receipts or change the partition layout. `poll_storage()`
continues in the existing loop while network checks run in their own owned task.
USB/microSD and file-management sources are unchanged. Existing file services
retain their storage ownership rules; this test does not grant simultaneous host
and firmware filesystem access.

BLE and public-assistance operations cannot overlap the diagnostic. Existing
dashboard/wake listeners stop before it starts. After it finishes or is cancelled,
normal authenticated setup remains available and the read-only dashboard can run
on a connected network. Saved-network fallback is suspended while the diagnostic
owns the station, so a fallback cannot be mistaken for Guest-Net success. Normal
firmware profiles keep their existing upload behavior; only this explicit image
pauses uploads for its entire boot.

## Build and enable

The default environment remains unchanged. Select this separate environment:

```sh
pio run -e tdongle_s3_product_guest_test
```

For the project's retained frozen SDK, use the existing guarded build helper in
the prepared build environment:

```sh
python3 tools/build_public_wifi_memory_profile.py --environment tdongle_s3_product_guest_test
```

Its distinct version is `0.4.13-guest-test`; output goes to
`artifacts/product-guest-test-build/`. The guarded helper verifies the frozen SDK
before and after, partition bytes and existing USB/storage review. It only builds;
it has no flash or serial action. A fresh generic PlatformIO installation may need
the retained project SDK preparation described in the developer guide; do not
silently replace this project's frozen SDK.

**Installing this diagnostic requires owner approval and the existing protected
installer/recovery process.** Do not run a blanket erase or an ordinary full-flash
upload against the owned stick. No automatic installation is configured here.

## Run on the stick after an approved installation

1. Plug the stick in normally. Keep its normal owner settings and microSD intact.
   The diagnostic needs an initialized owner; it does not bypass enrollment.
2. Open its USB serial monitor at **115200 baud**, with newline termination:

   ```sh
   pio device monitor -p /dev/ttyACM0 -b 115200
   ```

   Choose the actual stick port, not a phone or another serial device. Use only
   one serial client at a time; close Setup's diagnostic connection first.
3. Leave BLE/setup closed. Send `GUEST_TEST_START` once. Status prints at each
   phase and at completion. Use `GUEST_TEST_STATUS` to repeat the report.
4. Use `GUEST_TEST_CANCEL` to stop; a running network call must unwind safely
   before BLE can open. Leave at least15 seconds between scan starts.
5. Observe the LCD: searching, connecting, RSSI, connected/failed, then Internet
   Available, Login may be required, or Internet unverified. Existing faults and
   authenticated setup screens retain priority.

Example **expected successful output**, with invented addresses/signal values:

```text
GUEST_TEST phase=complete ssid=Guest-Net visible=yes security=Open auth=0 rssi=-58 attempt=1/2
  ip=192.168.20.44 gateway=192.168.20.1 dns_server=192.168.20.1 dns_http=ok dns_https=ok
  http=204 http_error=0 https=200 tls_verified=yes https_error=0 tls_error=0 clock=valid
  captive_portal=not detected internet=available radio_step=21 sdk_error=0 radio_error=20 disconnect_reason=0 uploads=disabled
  Internet available; no files uploaded
```

Expected failures include `Guest-Net not visible on 2.4 GHz`, an actual protected
security type, connection/DHCP failure after two attempts, DNS unverified, an
unsynchronized clock, or a TLS error. A zero HTTP status means no valid status
was obtained. A nonzero disconnect reason is the SDK reason number, not an HTTP
code. The report never says internet is available solely because DHCP succeeded.

## Disable and return to ordinary use

`GUEST_TEST_CANCEL` releases an active test and permits normal setup again;
when no test is active it leaves the current network/setup alone.
uploads remain disabled for this diagnostic image. To resume backups, install a
separately verified normal successor with version **0.4.14 or newer** through the
protected updater. Its downgrade gate prevents reinstalling an older normal image;
do not weaken that safeguard. Prepare the return image before installing this
diagnostic on the owned stick. For source
builds choose the ordinary product environment; **do not simply change the default
environment or remove safety flags from this diagnostic profile**. The
`CSP_ENABLE_GUEST_DIAGNOSTIC` flag defaults to0 and the build guard prevents it
from leaking into older image identities.

## Validation and next decision

Computer tests exercise actual diagnostic source with simulated radio, DHCP,
DNS, sockets, TLS and RTOS. They are not physical wireless or certificate-server
acceptance. Exact build identities and completed checks are in the
[development report](releases/2026-11-11-guest-internet-diagnostic.md).

Hardware acceptance still needs Guest-Net visibility, actual open association,
DHCP/gateway/DNS, real HTTP204 and verified HTTPS200, cancellation, LCD, USB/microSD,
authenticated BLE recovery and dashboard restoration. No actual workplace
connection, file upload or physical regression result is claimed for this image.

If those pass, propose adding Guest-Net as an **optional open saved profile** using
the existing authenticated Check → Save flow. Keep priority/enabled/delete controls,
the eight-profile limit and explicit confirmation. Do not auto-save a scan result
or treat an open network as trusted. No automatic cloud-upload change is included.
A Raspberry Pi is not needed merely to run this diagnostic; whether the stick can
replace one for this network depends on the actual hardware results.

References: [Espressif Wi-Fi API](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/network/esp_wifi.html),
[Arduino Wi-Fi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html),
[certificate-bundle verification](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/protocols/esp_crt_bundle.html).
