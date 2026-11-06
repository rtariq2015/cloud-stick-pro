# Developer guide

Cloud Stick Pro combines embedded firmware, a desktop setup application and an
Android viewer. Start with the [project map](PROJECT_LAYOUT.md), then read the
source for the build profile being changed. Older milestone reports record what
was known at that revision; they do not describe every later release.

## Find the right entry point

| Change | Start here | Relevant boundary |
| --- | --- | --- |
| Boot, setup or radio transitions | `src/main.cpp`, `src/wifi_setup_runtime.cpp` | Worker quiescence, exclusive radio ownership and acknowledged shutdown |
| USB and background backup | `src/usb_storage.cpp`, `src/sync_engine.cpp`, `src/pro_core.cpp` | Host is the SD writer; a host write invalidates a firmware read session before sector modification |
| Upload and verification | `src/webdav_client.cpp`, `src/pro_http.cpp`, `src/pro_journal.cpp` | A completed transfer is not a verified receipt; preserve conditional publication and readback checks |
| Bluetooth authentication | `src/ble_management.cpp`, `src/ble_sdk_lifecycle.cpp`, `sdk/pinned_ble/` | Espressif Security 2, terminal rejection and proven task/controller cleanup |
| Login, Wi-Fi or cloud settings | `src/owner_login_nvs.cpp`, `src/wifi_setup_nvs.cpp`, `src/owner_cloud_nvs.cpp` | Review/stage/check/explicit commit; preserve the previous durable record on failure |
| LCD or local dashboard | `include/pro_lcd_v2.h`, `src/lcd.cpp`, `src/local_dashboard_assets.cpp` | Display only active work as current; label recorded counts and timestamps accurately |
| Desktop application | `desktop/cloudstick_desktop/main.py`, `desktop/frontend/bootstrap.mjs` | Bundled local origin, typed native operations, exact device identity and cancellation drain |
| Android viewer | `android/app/src/main/java/org/cloudstick/viewer/MainActivity.java`, `android/app/src/main/assets/app.mjs` | Nearby BLE, personal login, read-only commands and lifecycle cleanup |

Detailed boundaries are documented in [storage safety](STORAGE_SAFETY.md),
[owner login](OWNER_LOGIN_FIRMWARE.md), the [desktop guide](../desktop/README.md)
and the [Android guide](../android/README.md). Preserve the upstream and dependency
notices described in [credits](CREDITS.md).

## Prepare a development change

Record the branch, source commit and working-tree changes before editing. Keep
unrelated work intact and use a separate branch or worktree for experiments.
Identify the smallest affected runtime and its tests before adding a new feature.
Keep historical release identities intact: a new candidate needs its own reviewed
profile and artifact identity.

The parent development workspace uses its `compose.sh` wrapper for commands;
follow its `AGENTS.md` when working there. Commands below are relative to this
project root and should run inside that configured development environment.
Public standalone checkouts should use the pinned dependencies and the relevant
application guide instead of copying machine-specific tool paths from old logs.

Firmware inputs are declared in `platformio.ini`: pioarduino `55.03.312`, the
original `tdongles3` board, TFT_eSPI `2.5.43` and ArduinoJson `7.4.3`.
The default `tdongle_s3` target remains BLE-disabled. Experimental and product
profiles are explicit opt-ins; selecting a profile is part of the review.

Build the default firmware without uploading:

```sh
pio run -e tdongle_s3
```

The guarded product build entry point is
`tools/build_public_wifi_memory_profile.py`. Its allowlist, `--check-only`,
`--self-test` and `--environment` options validate the exact profile and pinned
SDK inputs. For the existing setup-development profile, inspect the gate first:

```sh
python3 tools/build_public_wifi_memory_profile.py --check-only \
  --environment tdongle_s3_product_setup_dev
```

This is a profile/input check, not a firmware build or a device-readiness verdict.
Use the release's full guarded build and linked-symbol checks before selecting
an image. Keep `partitions.csv`, USB descriptors/pins and persistent formats
unchanged unless their migration is separately designed and tested. A build
command must never imply upload, erase, format or automatic device reset.

## Test the affected behavior

| Area | Source/desktop test entry points |
| --- | --- |
| Core backup, journal and WebDAV | `bash test/run.sh` |
| BLE lifecycle policy | `bash test/run_ble_native.sh` |
| Pinned Security 2 and SDK lifecycle | `test/milestone1/`, `test/milestone1_corrections/`, `sdk/pinned_ble/README.md` |
| Guided setup windows | `test/setup_duration/management_tests.py`, `lifecycle_tests.py`, `security_tests.py` |
| No-change scan gate | `python3 test/scan_idle_gate/run.py` |
| LCD model and actual rendering adapter | `python3 test/lcd_v2/run.py`, `python3 test/lcd_v2/render.py` |
| Local dashboard parser/server/UI | `python3 test/local_dashboard/run.py` |
| Desktop native boundaries | `PYTHONPATH=desktop python3 -W error -m unittest discover -s desktop/tests -p 'test_*.py'` |
| Desktop UI and wizard | `node --test desktop/tests/frontend-*.test.mjs` |
| Android UI and native lifecycle | `node --test android/tests/*.test.mjs`; Gradle commands in `android/README.md` |

`tools/test_product_source.py` records the wider source regression matrix. Some
older harnesses refer to the prepared development tool environment; inspect their
inputs before assuming they run on a fresh machine. Native tests need the declared
compiler, sanitizer, Python/Node and relevant development-library prerequisites.
Desktop Qt smoke tests require the pinned Python/Qt environment and a supported
display, even when their device transport is synthetic.

Test a behavioral boundary rather than repeat the implementation in a test.
Useful cases include cancellation during a pending response, a changed USB
identity, a wrong authenticated frame, a failed persistent-record readback, a host
write during cloud verification, and inactive status retaining old progress fields.
Run strict sanitizer harnesses where available. A nonzero exit, sanitizer report
or unexpected diagnostic output must remain a failed gate.

## Preserve lifecycle and data guarantees

Firmware reads the host-owned FAT volume through generation-bound sessions.
A quiet period helps choose when to try; it cannot prove that the host application
closed or flushed a file. Keep generation checks at the physical I/O boundary and
retain the original local file when an upload, verification or publication fails.
Receipts describe verified copies, not transfer acknowledgments.

Bluetooth setup pauses conflicting background work and owns a finite setup
session. Authentication failure is terminal for that session. Disconnect or task
stop requests are not proof that cleanup finished: retain ownership and block
conflicting operations if shutdown cannot be established. Do not add resets,
watchdog extensions or insecure authentication/TLS fallbacks to hide failures.

Desktop and Android operations similarly bind replies to their session/generation.
Late callbacks must not restore authentication, expose private results or advance
a wizard after cancellation. Review is not Save; an uncertain save outcome needs
fresh state and authentication instead of an automatic duplicate write. First-owner
creation applies only to a genuinely Missing owner under the firmware's physical
gates; it is not account recovery for an already owned device.

## Record a reviewable revision

For each candidate, retain the source commit, changed behavior, profile and
dependency identities, app/partition hashes, test commands/results and remaining
physical acceptance work. Use the [progress and release index](PROJECT_HISTORY.md)
and revision history to keep the order of changes clear. Do not rewrite a
historical PASS to imply a later image or different hardware was tested.

Separate source simulation, real desktop rendering, hardware measurement and
cloud readback evidence. Record actual transfer phase/units and measured memory
headroom; a faster buffer setting alone is not a speed result. Before a hardware
update, follow the reviewed release recovery procedure with a fresh protected
backup and quiescent storage. Synthetic results do not authorize a factory writer
or establish compatibility with an unknown board.

Keep passwords, owner records, private recovery copies, signing keys, clinical
files and raw private logs out of Git and public artifacts. Release manifests
should enumerate intentional public files rather than package a working directory.

## Write comments that preserve reasoning

Explain ownership, ordering and failure behavior where the code cannot express
them alone. For example, document why a host write invalidates a snapshot *before*
the first sector changes, why cleanup retains a failed radio owner, or why an
asynchronous reply belongs to a particular generation. State a bounded limit and
its source when that limit protects memory or protocol framing.

Avoid comments that merely repeat an assignment, claim a test was performed
without evidence, or bury release history in active code. Put revisions and
measurements in reports; keep current invariants beside the implementation.
