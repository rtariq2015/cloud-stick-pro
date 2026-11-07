# Saved Wi-Fi fallback candidate

Firmware **0.4.4-wifi-dev**, profile `tdongle_s3_product_wifi_dev`, branch
`fix/saved-wifi-fallback`. This is tested development source, with **no new
firmware binary, installation or physical acceptance**. Android remains 0.1.2
and desktop remains 0.5.3. Existing prerelease downloads are unchanged.

## Behavior

The existing Pro setup firmware retains up to eight enabled/disabled saved
networks. An owner can add a network through the authenticated desktop wizard's
Find Wi-Fi, Check and Save flow; a different network does not replace the other
saved entries. Networks not explicitly saved are never automatic candidates.
The Android viewer does not save Wi-Fi credentials.

When Wi-Fi disconnects, the owner loop retires and reaps the backup worker before
starting another connection. A successful local link is kept. The existing retry
order prefers the selected network, then its existing priority/ID ordering,
subject to a 10-second to five-minute exponential cooldown.

A connection attempt can take 25 seconds, longer than the first cooldown. The
old policy could therefore retry a preferred failed network before all saved
alternatives had a turn. The new opt-in policy first considers the eligible
networks with fewer consecutive failures, then applies the existing ordering.
It uses the existing eight bounded failure records, without extra persistent
state, allocation or saved-setting writes. A working link does not trigger
switching. Configuration revisions reset retry history; a successful connection
clears that profile's failures. Cooldowns still apply when all networks fail.

This handles Wi-Fi disconnection. Remaining connected to an access point whose
Internet service is down does not trigger automatic network switching. A captive
portal may still need the owner's acceptance. Local association does not prove
Internet access or a verified cloud backup.

Historical/default profiles keep the previous retry policy. The new candidate
inherits the UI/incremental profile and enables `CSP_ENABLE_WIFI_FAIR_RETRY=1`
under its own version identity. Partitions, USB storage ownership, receipts,
authentication, TLS validation and worker shutdown requirements are unchanged.

## Verified source tests

* The eight-network regression failed against the original policy, reproducing
  a repeat before all enabled profiles were tried; it passes with the change.
* `python3 tools/test_wifi_reconnect.py`: historical, explicitly disabled and
  enabled builds/runs pass with ASan, UBSan, leak checks and strict warnings.
  The new path covers eight retry rounds with 1-second and 25-second failures,
  cooldown saturation, clock rollover, repeated polls, all-disabled networks,
  success reset and configuration revision changes. Original policy tests retain
  priority ties, disabled profiles, corrupt counts and no-setting-mutation checks.
* `tools/test_backup_runtime.py`: 53 successful records, including compilation
  and runtime scenarios, with the real owner-loop/NVS adapter source and
  simulated radio, worker, USB, BLE, NVS, clock and heap. The new eight-network
  scenario reaches network eight after slow failures, proves no connection
  change before worker reap, restarts the worker on recovery and retains that
  connection through a further simulated minute. Existing failure/cleanup
  scenarios and persistent-setting comparisons pass. No sanitizer diagnostics.
* Five build-configuration tests pass, including candidate/parent inheritance,
  distinct version strings, isolation of feature bits and separate output paths.
  These use the real PlatformIO configuration with a synthetic board-reference
  fixture; they do **not** verify the frozen SDK or constitute a firmware build.
* Actual feature-header admission tests pass, including rejection of invalid
  fair-retry values. GitHub Actions now runs the native reconnect policy test;
  the full runtime simulation is a separate local check.

Runtime tests used Ubuntu Clang 18.1.3, unchanged `-O1`, strict warnings and
sanitizers, with pinned ArduinoJson 7.4.3 headers. On a prepared checkout, run:

```sh
CSP_TEST_CLANGXX=/absolute/path/to/clang++ python3 tools/test_backup_runtime.py
python3 tools/test_wifi_reconnect.py
python3 test/product_rc/profile.test.py
```

Preparation failures were investigated: GCC's existing optimized ArduinoJson
warning prevented compilation; Clang found an owner-window helper unused when
both owner cloud/login features are disabled, now excluded for those builds.
Resolving the Clang++ symlink to the C driver name initially omitted C++ runtime
linkage; retaining the C++ driver name fixed the test invocation. These failed
preparations are not counted as passes or hardware evidence.

## Source identities and remaining gates

| Input | SHA-256 |
| --- | --- |
| `include/pro_wifi_reconnect.h` | `ac5fe0eeda19d86121eb78d35b1018088ea655a7fd84bb696add4201683f1d17` |
| `src/wifi_setup_runtime.cpp` | `7c985db7da298d127218cccb6f5c6e64cbdbafd4fdbb2637dee087e582becf49` |
| `platformio.ini` | `5508db78d93c92142c076561c26519840ea9581445dca6a552e8148b332ae600` |

The reviewed-source manifest records the committed snapshot and per-file hashes.
There is no candidate app hash, size or memory measurement because no candidate
image was built. The guarded `--check-only` attempt failed closed because the
reviewed frozen SDK's reuse guard is absent on this machine. No stock SDK was
substituted and no build guard was bypassed.

Before binary release or installation: restore and verify the exact frozen SDK,
complete the guarded app-only build and record artifact/resource identities, then
perform separately scoped physical network-loss and backup/readback acceptance
with a fresh recovery checkpoint. Test multiple saved authorized networks,
unavailable networks, all-networks-down recovery and disconnection during upload.
No device, serial port, live cloud account or user file was accessed in this work.
Attribution and existing source/binary gates remain in [Credits](../CREDITS.md).
