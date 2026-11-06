# Phone prototype provenance and security boundary

This is a **synthetic-only Milestone 1 source prototype**. It is not installed on
the dongle and has no real Wi-Fi, cloud or owner secrets. The application sends
only authenticated `info` commands and cannot change device configuration or files.

## Protocol and cryptographic implementation

`security2.mjs` is a browser port of the **existing Espressif Security 2 protocol**,
not a new protocol or cipher. SHA-512, random generation and AES-256-GCM use the
browser's Web Crypto implementation. SRP6a arithmetic, byte encoding, proof and key
derivation follow these pinned ESP-IDF **v5.5.5** sources:

- [security2.py](https://github.com/espressif/esp-idf/blob/v5.5.5/tools/esp_prov/security/security2.py)
- [srp6a.py](https://github.com/espressif/esp-idf/blob/v5.5.5/tools/esp_prov/security/srp6a.py)
- [session.proto](https://github.com/espressif/esp-idf/blob/v5.5.5/components/protocomm/proto/session.proto)
- [sec2.proto](https://github.com/espressif/esp-idf/blob/v5.5.5/components/protocomm/proto/sec2.proto)

The unmodified SRP reference is retained under `test/reference/srp6a.py` with Git
blob `2b543764c0fd4777d7df34c369d1506ea245bdc7`. The minimal test-only `utils`
adapter supplies integer/byte conversion. `reference_vectors.py` generates only
public synthetic vectors. Espressif's Apache-2.0 license is included in
`vendor/ESPRESSIF-LICENSE`; the port's modifications are marked in its header.

This port requires Security 2 **patch 1**. It verifies the device's 64-byte SRP
proof, requires a 12-byte nonce, increments the shared big-endian counter and
fails closed on a bad tag, replay, exhausted counter, wrong scheme or malformed
packet. It has no Security 0/1 or static-nonce fallback. GATT messages are bounded
and serialized; a timeout requires disconnect and a new authentication session.

JavaScript BigInt arithmetic is not constant-time. Web Crypto does not expose SRP,
and JavaScript/garbage collection does not promise complete memory erasure. Typed
secret buffers are cleared and references dropped on disconnect, but this is not
a production credential-protection claim. Actual phone, GATT, radio and ownership
enrollment remain hardware/review gates. The test fixture (`wifiprov` / `abcd1234`)
is deliberately public and must never be treated as real device ownership.

The SDK legitimately serializes server B as a variable-width integer, so the port
accepts1–384 bytes with strict range checks; client A remains full384 bytes to
match the SDK's raw proof input. A deterministic383-byte B regression covers that
case. There is a separate official Python/raw-C difference for leading-zero salt
or the inner credential digest: Python converts them through an integer, while
the C implementation hashes the raw octets. M1 deliberately rejects these
unsupported encodings and only supports its fixed canonical synthetic credential
fixture. Arbitrary production credential/salt compatibility is **not** claimed;
its raw-octet semantics need separate review before a production client.

## Rejected browser dependency

The earlier research named `nikas-belogolov/esp-ble-prov` as a candidate requiring
review. During this implementation review, its master commit
`fdd1e9e90366b873b5a50258fe84b2d62a1b7cb2` added a workflow named “Test Suite” that
searches repository/history/wiki content for credentials and sends matches to an
external destination. We read that source; we did **not** execute its workflow,
install scripts or copy its runtime implementation. It is rejected as a dependency.
See the [specific source change](https://github.com/nikas-belogolov/esp-ble-prov/commit/fdd1e9e90366b873b5a50258fe84b2d62a1b7cb2).

The published npm 2.0.1 archive was downloaded with scripts disabled for inspection
only. Its checksum is SHA-1 `33e2f00e2162db2f55c0d850efb028e04a8fce82`; it is not
installed, vendored or used by this interface. No third-party runtime dependency,
CDN, telemetry or network proxy is present in this phone application.

## What testing means

- Node tests compare SRP values against the unmodified official Python reference.
- The same browser client is exercised against actual SDK Security 2 C code
  compiled for the desktop, including ASan/UBSan/LSan variants.
- Chromium renders 320/390/800-pixel states and runs Web Crypto against that SDK
  host through a **mocked GATT** transport. This is not Bluetooth radio testing.
- `?preview=idle`, `?preview=connected` and `?preview=error` are visual-only states.
  They show a prominent simulated-data banner and never call Bluetooth APIs.

The real page must be served from a trusted HTTPS origin in Android Chrome. A
localhost origin is acceptable only for desktop development. The current dongle's
HTTP dashboard is not a Web Bluetooth secure context. Nothing has been deployed.
