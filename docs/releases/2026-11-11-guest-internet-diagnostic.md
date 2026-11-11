# Optional guest internet diagnostic — 11 November 2026

Development firmware **0.4.13-guest-test** adds a serial-controlled Guest-Net
connection and internet test to the existing product runtime. It is default-off,
does not save a network and never admits the cloud worker during that boot.
No firmware was flashed, no phone/network setting changed and no user file or
cloud upload operation performed for this work. Workplace acceptance is pending.

The owner reports successful laptop HTTP204 and HTTPS200 without a portal. That
is user-reported laptop evidence, not a test of this image on the dongle. Earlier
diagnostic images and earlier work-network failures have separate identities.

## Change and preserved boundaries

The target-only scan reports raw security, RSSI and whether matching results were
truncated. Protected/conflicting/truncated scans stop without connecting. An open
network uses the existing RAM-only adapter, DHCP lease validation and at most two
25-second attempts. An SDK disconnect reason is retained for the report.

DNS and fixed HTTP/HTTPS GET requests run in an owned task. HTTPS uses the pinned
Mozilla bundle, hostname verification and certificate dates. The custom response
reader has a 4096-byte budget, a bounded status line and a five-second whole-header
deadline; it stops before the body. No redirects, cookies, portal acceptance or
credentials are sent. TCP and TLS timeouts are finite; the SDK DNS resolver can
take longer than a socket timeout. Cancellation waits for normal unwinding and
suspended-task reclamation, without deleting a live task.

The owner loop continues USB polling. It blocks BLE opening, normal reconnect and
dashboard admission while the diagnostic owns the station. Setup and dashboard
can resume afterward; uploads stay disabled until a normal image is installed.
Idle cancellation does not disturb other provisioning. LCD rendering stays in the
existing owner loop and marks a completed result as old if the link later changes.

USB/microSD, block ownership, file/receipt code, authentication, cloud settings,
watchdogs, partitions and all pinned SDK inputs were retained. The build guard
checks the existing incremental storage source review and used USB buffer sizes;
this does not turn physical storage regression testing into a software result.
No normal download/catalog entry, immutable release asset or component app was
replaced. The [credits and source/binary gate](../CREDITS.md) remain in effect.

## Completed software checks

| Check | Result and scope |
| --- | --- |
| Diagnostic orchestration | 50 scenarios; actual source, simulated radio/DHCP/probe; ASAN/UBSAN |
| Bounded internet probe | 34 scenarios /481 assertions; actual source, simulated DNS/sockets/TLS/RTOS; ASAN/UBSAN |
| Owner-loop integration | 8 scenarios; actual runtime/model/NVS adapter, simulated I/O; uploads excluded, USB polling continues |
| Wi-Fi integration | 54 build/run steps pass; actual parser/store/runtime/SDK crypto with simulated hardware |
| Radio adapter | 128 historical, 162 release and140 guest-profile assertions; filtered scan, security distinctions, DHCP and disconnect reasons |
| Existing backup runtime | 78 steps across8 native builds pass; includes historical enabled/disabled policy and wire cases |
| Profile/SDK guard | 61 self-tests pass; diagnostic flag refused in historical identities |
| Publication policy | 36 tests pass; explicit new source/test/guide allowlist only |
| Target compilation | Guarded PlatformIO/Arduino build passes with6509 frozen SDK entries checked before and after |

The retained SDK manifest SHA-256 is
`1667e4e5650d161240a3ba6a271f02004717ddc1caec56cf178805f8e0738237`.
The build confirms unchanged partition bytes and used USB buffers. Static RAM is
84,652 bytes. No device is attached to the network-disabled build container.
The first compile found and corrected an ambiguous Arduino IPAddress assignment.
Tests also caught a stale success message and the test fixture's incomplete
security enumeration; both were corrected before this passing result.

The reviewed local application is1,679,584 bytes, SHA-256
`d33940f61c0ffc0cb9c783f2b415f874e613dbdfecb8462445a6df2249ebdee9`.
The local guarded source snapshot digest (sorted compact JSON of source-file
size/hash identities) is
`0ae9d407b7911866f384b98a5aebaaf5d0deb159c848b7af5c7c12e0c4ddc9dd`.
These are development build identities, not an offered or hardware-approved
firmware package. Committed-build and GitHub-check evidence is recorded with the
completed publication milestone.

## Physical checks still required

This task has not tested actual Guest-Net visibility, security, association,
DHCP, gateway/DNS, HTTP204, verified HTTPS200, RF strength or captive-portal state
on the dongle. Actual LCD, USB/microSD activity during requests, BLE recovery,
dashboard restoration, memory margins and endurance also remain unverified on
this exact image. Simulated TLS failure tests do not validate a real certificate
chain. No claim is made that older BLE cleanup or cloud-upload faults are fixed.

Installation requires separate owner approval and the existing protected backup,
app-only preservation and readback process. See the
[step-by-step guide and expected serial output](../GUEST_WIFI_TEST.md).

After physical success, an optional Guest-Net open profile can be proposed through
the existing authenticated Check → Save flow, retaining enabled/priority/delete
controls and the eight-network limit. This work does not auto-save it or enable
automatic cloud uploads.
