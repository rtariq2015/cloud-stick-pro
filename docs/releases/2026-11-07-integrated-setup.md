# Setup 0.6.0 — all-in-one desktop preview

Setup now includes reviewed firmware installation and update on Windows and
Linux, followed by the existing login, Wi-Fi and cloud wizard. The finish screen
reminds a newly created first account to return the microSD while unplugged if
it is still out. Firmware remains **0.4.6-cache-dev**; Android remains **0.1.2**.

## Code and data identities

| Component | Identity |
| --- | --- |
| Desktop runtime/UI source | `d6aeb6709a5b94e6a73eed4cc00b402000facd30` |
| Reviewed source merge | `a2219baba5f99712366d38bafc26d646aecadbdc`, PR #11 |
| Firmware source | `adc439bcc69802d289fa79b34f9994f4093d0f83`, unchanged |
| Firmware app | 1,691,232 bytes; SHA-256 `b0d5984282d5ed41f2dfa99558aca38aae8b865102d959de3ff61118228554a3` |
| Signed offline install data | `firmware-data-0.4.6-r1`; `.cspfw` SHA-256 `c571be6746dc361861ddae2bbd0b6607445e6f39945f39b7819c62400ae20d15` |

The final release manifest records each package's exact packaging source commit,
size and SHA-256. Documentation/package-guide revisions can follow the runtime
base above without changing product runtime sources. Old releases remain intact.

## What changed

The GUI checks signed metadata from the project release key, verifies the bounded
six-image package and offers a native confirmation. An independent installer
checks the selected board and security state. It makes two encrypted full 16 MiB
recovery reads, reopens/authenticates them and compares every byte before writing.
Existing sticks receive an app-only update. New-board installation requires an
erased or reviewed manufacturer preimage, no existing ownership and an empty
target journal. Unknown firmware and incompatible data block installation.

Full-flash read-back checks the expected image overlay and preserved bytes.
Normal boot follows verification. An interrupted write never retries by itself;
explicit recovery checks the same board, original checkpoint and protected state.
Window closure waits for active work. Windows job identity includes process
creation time; Linux requires its actual worker's zero-swap cgroup gate.

Only a verified catalog signature admits compatible later firmware to the same
strict management contract. Breaking catalog contracts ask for a newer app.
Local manifest review remains read-only and cannot authorize a write.

## Verification actually performed

| Check | Result and scope |
| --- | --- |
| Desktop interface tests | 132 pass; synthetic/native bridge and wizard logic |
| Linux Python tests | 195 cases, one Windows-only skip; includes full simulated coordinator |
| Windows Python tests | Native OS protection plus protocol/fault tests; POSIX-only fixture skipped |
| Full coordinator | Actual signed public package/crypto with fake ROM: first install, explicit interrupted resume, wrong-board refusal before reading its flash, mismatched recovery reads and post-boot failure |
| Empty journal | Mounted read-only by actual frozen-SDK LittleFS C sources using the firmware's 128-byte read/program configuration; exactly an empty `/queue` |
| Native packages | Linux archive/`.deb`, Windows ZIP/installer built on their native OS |
| Packaged programs | GUI secure-origin/WebCrypto smoke and installer dependency/security self-test pass with synthetic data |
| Windows installation | Fresh per-user install, installed GUI/helper tests and uninstall pass on Windows Server 2025 CI; no hardware contact |
| Existing backup core review | 70/70 native/mock-WebDAV cases pass under address/undefined-behavior sanitizers; no hardware contact |

The runtime build/check run is
[42380012605](https://github.com/rtariq2015/cloud-stick-pro/actions/runs/42380012605).
Exact later packaging runs and artifact identities are attached to the release.
The earlier normal/service firmware acceptance remains in the
[0.4.6 report](2026-11-06-ble-cleanup.md); it is not relabelled as new-board proof.

## Still pending

- An actual first installation on an unconfigured original T-Dongle S3. The owner
  is planning to buy a second board; the owned stick was not erased to simulate it.
- Physical Windows USB/Bluetooth interaction and packaged-GUI personal settings use.
- The exact personal phone Save Wi-Fi → Finish sequence, actual AP power-off,
  a new exact-image cloud-upload trial and multi-day endurance.

This remains a development preview. The firmware application, partition layout,
USB-storage ownership, authentication, TLS, watchdogs, receipts and user-file
policy are unchanged. Recovery copies/signing keys/raw device logs remain private.
The [credits and source/binary gate](../CREDITS.md) retain all component notices
and the unresolved upstream license-text evidence gap.
