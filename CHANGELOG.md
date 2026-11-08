# Changelog

## 8 November 2026 — Android Viewer 0.2.1

- Reply immediately when a well-formed request contains an invalid local address.
- Preserve useful initial Bluetooth connection/sign-in guidance during cleanup.
- Add reproducible signed emulator checks, real-stick LAN and separate host-Bluetooth evidence.
- Keep firmware, saved settings, user files and private-history authentication unchanged.


## 8 November 2026 — Android Viewer 0.2.0

- Open directly to a live Wi-Fi dashboard; remember the stick’s IP on this phone.
- Show current activity, transfer progress and scan counts without Bluetooth or a login.
- Keep authenticated private file history separate, with acknowledged Bluetooth closure.
- Mark disconnected readings as old, stop polling in the background, and refresh on return.
- Preserve firmware, desktop versions, saved settings and user files.


## 2026-11-07 — all-in-one Setup 0.6.0 and clearer project help

- Windows/Linux Setup includes signed firmware checks/downloads, guarded first
  installation and existing-stick updates, private recovery and full read-back.
- Native OS builds, packaged tests and Windows install/launch/uninstall pass in
  CI. New-board and physical Windows USB/Bluetooth acceptance remain pending.
- Add the first-account microSD reminder and keep login/Wi-Fi/cloud in one flow.
- Reorganize the README, help, folder indexes and historical source records;
  refresh setup imagery and preserve runtime paths and old artifacts.
- Exclude the new private recovery format from publication/source exports.


## 2026-11-07 — current preview guides and screenshots

- Update current download links and version tables to firmware 0.4.6 and Setup
  0.5.7, and explain Diagnose my stick in the getting-started guide.
- Refresh the desktop welcome preview and add a clearly labelled synthetic
  troubleshooting screenshot. Android and LCD previews are unchanged.
- Preserve historical release identities and the separate hardware-test limits.
  This documentation refresh does not change application or firmware binaries.

## 2026-11-07 — physical cache and reconnect milestone

- The private service image passed real NVS cache churn, repeated Bluetooth
  cleanup and a controlled round trip across two saved Wi-Fi profiles. Backups
  resumed with five verified files, unchanged revision and no reported errors.
- The normal image is installed and passed two more closes with backups resuming.
  Protected state and logical settings comparison passed. The personal
  authenticated Save/Finish flow remains unverified. See the [report](docs/releases/2026-11-06-ble-cleanup.md)
  for exact images, memory measurements and limits.

## Desktop 0.5.7 — guided local troubleshooting

- “Diagnose my stick” explains reported Bluetooth cleanup failures, paused
  backups, missing Wi-Fi and incomplete scans, with a concrete next step.
- Guidance uses the existing private-data-free local snapshot. It makes no
  automatic repairs, account requests, settings changes or firmware writes.

## 2026-11-07 — NVS cache-release candidate

* The real authenticated 0.4.5 save/close trial failed at the 30-second memory
  deadline. Saved revision 9 and five verified files were retained; read-only
  diagnosis and normal recovery completed without a firmware/settings write.
* Candidate 0.4.6-cache-dev rebuilds the SDK NVS RAM cache after verified radio
  and worker teardown, with an exact saved-partition comparison and unchanged
  memory guards. Desktop 0.5.6 admits only that exact normal candidate.
* Add a separate bounded service fixture for synthetic NVS cache churn and
  controlled Wi-Fi link loss; its commands/data must be absent from normal
  images. Both builds and local source/simulation checks pass. Physical
  normal-image acceptance subsequently passed; see the [report](docs/releases/2026-11-06-ble-cleanup.md).

## 2026-11-06 — Bluetooth cleanup source fix

* Firmware candidate `0.4.5-ble-dev` lets verified Bluetooth shutdown drain
  delayed SDK cleanup within the existing 30-second closing deadline. The old
  two-second limit could leave backups paused after a successful Wi-Fi save.
  Preserve the full contiguous-memory proof, final memory thresholds, verified
  stop, USB ownership and failure lockout. Older build profiles keep their limit.
* Desktop `0.5.5` recognizes only the exact new candidate alongside the existing
  reviewed versions. Android remains `0.1.2`. This source milestone does not
  claim installation or authenticated setup-close acceptance on hardware.

## 2026-11-06 — installed Wi-Fi fallback and desktop compatibility

* Firmware `0.4.4-wifi-dev`: guarded SDK/app build, two matching fresh encrypted
  recovery reads, app/protected-state readback and controlled normal return passed.
  Native health, setup-close Wi-Fi reconnection and a new PDF/independent full
  cloud hash passed on the board; settings,
  receipts, USB ownership and user files retained. Distinct-network physical
  switching remains unvalidated; eight-network retry simulations pass.
* Desktop `0.5.4`: admit exactly the reviewed new firmware for USB setup/health.
  Linux packages, 3,008 installed file hashes and native smoke passed; earlier
  installation preserved. Android stays `0.1.2`; no native Windows binary claimed.
* See the [Wi-Fi update report](docs/releases/2026-11-06-wifi-fallback.md) for exact
  source/artifact hashes, completed checks, preparation failures and remaining gates.

## 2026-11-06 — initial saved Wi-Fi source milestone

* Firmware `0.4.4-wifi-dev`: give less-tried saved networks a turn before
  retrying unavailable preferred networks, including slow connection failures.
  Keep a working connection, capped retry cooldowns and worker retirement.
* Add strict native policy checks to GitHub Actions. Eight-network failover and
  backup shutdown passed runtime simulation; no new firmware image is built or
  installed. The reviewed SDK snapshot and physical acceptance remain required.
  See the [candidate report](docs/releases/2026-11-06-wifi-fallback.md).

## 2026-11-06 — modern companions and LCD source

* Desktop `0.5.3`: blue/white setup wizard with numbered rail and clear Back/Next
  actions; shared Windows interface and build recipe updated. Linux packages and
  installed file hashes/smoke verified; no native Windows binary is claimed.
* Android `0.1.2`, code 3: compact personal login, prominent Verified count,
  recent saved files and clear Done/Close Bluetooth action. Same application and
  signer; 35 JVM, 41 JavaScript and 31 visual checks passed. Phone acceptance of
  this exact signed APK remains pending.
* Firmware `0.4.3-ui-dev`: navy LCD, explicit activity badges and one progress bar;
  existing fonts, 88-byte frame, zero render allocations and cadence retained.
  Installed after fresh encrypted recovery checks; independent app/protected-range
  checks and one new automatic PDF/full cloud hash passed. Native health passed.
* Organize an independent Cloud Stick Pro VS Code/Git project, with component
  sources and guides together and local releases ignored by Git.
* Installed `0.4.2` passed two automatic 2 MiB PDF backups with independent full
  cloud hashes, a BLE lifecycle cycle and a 405-second quiet observation. Raw
  upload rate remains about 50–54 KiB/s; reduced checking is the measured-work
  improvement, not a new raw-speed increase.

## 2026-11-06 — incremental firmware and local companion candidates

* Firmware `0.4.2-incremental-dev`: optional bounded sector-write coverage and
  receipt-bound file-layout proofs skip unchanged verified content. Same-size
  edits, invalidation, overflow and daily local audit retain full-hash fallback.
* Clarify LCD/LAN phases, inactive/partial results and stale progress fields;
  retain fonts, redraw cadence, partitions and the host-only SD writer model.
* Earlier desktop `0.5.1` and Android `0.1.1`: clearer recorded-status cards, useful errors,
  readable history, fewer redundant reads and native-close acknowledgment.
* Add developer notes, upstream/component credits, safe synthetic screenshots,
  ordered release evidence and a committed-source-only packaging tool.
* Both firmware profiles and both apps build; detailed checks, resource deltas,
  hashes and physical/publication limits are in the
  [improvement report](docs/releases/2026-11-06-improvements.md).

## 2026-11-06 — preserved companion suite baseline

* Preserve firmware `0.4.1-setup-dev` (`c6ec0e0`), Linux `0.5.0` (`c76d1cc`) and
  Android `0.1.0` (`0bf3d96`) together at combined baseline `d4ab3bb`.
* Local desktop setup and Android viewing no longer require a hosted phone site.
  Personal login and existing owner/security policies remain separate from a
  generic new-board firmware installer, which is not enabled.

## 0.4.0-rc4 — source-only receipt-inventory follow-up, 2026-11-05

* Preserve valid historical receipts in the quiet-scan inventory proof while
  retaining the independent complete, verified current-folder scan gate.
* Add a distinct guarded RC4 build with the reviewed RC3 feature set; keep RC3
  configuration and default BLE-disabled production behavior unchanged.
* Clarify local dashboard setup instructions for a chosen username/password or
  the existing private owner key. Physical RC4 acceptance remains pending.

## 0.3.3-wifi-m2-start2 — source-only candidate, 2026-11-03

* Remove duplicate scan-list cleanup after stopping Wi-Fi; enforce the pinned
  SDK's running-driver requirement in the fixture and reproduce diag1's observed
  boot fault from its exact source.
* Record scan/cancel/cleanup errors and explicit unattributed faults; expose the
  numeric policy error in sanitized serial diagnostics.
* Preserve partitions, USB/SD behavior, settings, auth and memory guards. New app
  is locally tested and built; installation requires its separate approval.

## 0.3.3-wifi-m2-diag1 — source-only candidate, 2026-11-03

* Correct DHCP-stop ordering after the queued Wi-Fi stop barrier and model the
  pinned SDK reset/STOPPED requirements in the radio fixture.
* Add sanitized serial startup step/error diagnostics after the first M2 hardware
  boot reported a generic Wi-Fi fault. Physical attribution/fix remains unverified.
* Preserve previous images/evidence and default production behavior. No new
  image installation, owner enrollment, SD change or publication in this correction.

## 0.3.3-wifi-m2-dev — source-only integration, 2026-11-03

* Add a nondefault setup-only target for fixed private-owner Bluetooth Wi-Fi
  scan, staged connection checks and explicit durable save.
* Keep normal production BLE-disabled; exclude cloud/receipt/legacy web workers
  from M2. Preserve partitions and USB storage source/ownership behavior.
* Add bounded protocol/transaction/store, first-owner enrollment, new phone
  source and local five-file HTTPS review package. No real key, device install,
  live credential change or site deployment in this stage.
* Validate source with strict native/SDK/UI regressions and three target builds;
  physical RF/USB/memory/security acceptance remains pending. See
  `MILESTONE2_WIFI_INTEGRATION_REPORT.md`.

## Unreleased Wi-Fi setup source experiment — 2026-11-03

* Add a standalone eight-profile candidate policy with owner-context gates,
  redacted snapshots, validation tickets, revision checks and memory-only commit.
* Add an example-only local phone setup preview and strict model/UI tests.
* Keep the new module outside both firmware links. No installed behavior,
  credential, storage, BLE protocol or published phone-page change.
* Browser rendering remains unverified because the container cannot start
  Chromium's sandbox. See `WIFI_SETUP_SOURCE_PROTOTYPE.md` for evidence and limits.

## 0.3.3-ble-m1 — source prototype, 2026-11-03

* Add a separate synthetic-only BLE target; production default remains disabled.
* Require physical entry and explicit Espressif Security2 patch1 for read-only
  device identification/status. Gate legacy settings paths in the prototype.
* Serialize BLE maintenance with backup/Wi-Fi activity and fail closed on memory
  or teardown errors without resetting host-owned USB.
* Add a dependency-free browser protocol prototype, official SDK desktop
  interoperability tests and lifecycle/parser/security regressions.
* Preserve partition layout and storage/configuration/receipt implementations.
  No hardware install or physical BLE acceptance has occurred.

## 0.3.3-dev — 2026-11-03

* Show the active filename and live upload/verification state on the web dashboard;
  retain plain-text rendering and wrap long filenames on narrow phones.
* Keep filenames out of downloadable diagnostics; saved receipts remain separate.
* Reject an unexpectedly short SD hash read. Treat a source invalidated before
  upload/verification as a normal interruption instead of an invalid-file error.
* Native checks: 70 passed. Browser-script checks: 24 passed. Chromium checks at
  320/390/800 px passed, including a long active filename.
* Physical 32 MiB PDF upload and full cloud readback passed, including a restart
  before verification. An independent cloud download matched SHA-256. The mobile
  dashboard passed a 37-minute soak, and a file above 64 MiB was correctly skipped.
* Three further unchanged-file scans passed without duplicate uploads, errors or
  resets. Normal backups keep the drive mounted and require no computer helper.

## 0.3.2-dev — 2026-11-03

* Batch aligned USB sector transfers through a fixed 2 KiB DMA buffer. Preserve
  partial-sector bytes, range checks, serialization and write-generation invalidation.
* Recover the last verified timestamp from persisted QA receipts at startup.
* Add exhaustive offset/boundary and I/O-failure regressions: 70 native checks
  and 23 browser-script checks passed; firmware and Chromium checks passed.
* Physical 8,389,349-byte USB write/fsync took 13.37 seconds; independent raw-card
  filesystem readback matched SHA-256. Larger cloud and transition tests continue.

## 0.3.1-dev — 2026-11-03

* Cancel source transfers when setup is requested, so long transfers do not delay maintenance.
* Timestamp verified copies and retry deadlines at transfer completion.
* Refresh the first receipt page automatically; keep manually paged records stable.
* Show skipped-file counts and actionable file-limit errors; display available SD space.
* Distinguish interrupted network/timeout transfers from genuine SD read failures.
* Disable station modem sleep on this continuously USB-powered device; measure
  throughput on the hardware before claiming an improvement.
* Core regressions: 67 passed. Browser-script regressions: 23 passed.

## 0.3.0-dev — 2026-11-03

* Add authenticated read-only LAN dashboard while backups continue: transfer
  phase, bytes, speed, latest-scan counts, timestamped file receipts and logs.
* Keep configuration changes on the protected setup AP; preserve fixed credentials.
* Default the watched folder to QA, as requested; preserve nested relative paths.
* Stream web assets from flash and paginate receipts four at a time.
* Add color LCD states, progress bar, transfer rate and idle dashboard address.
* Fix Koofr existing-file replacement: tagged conditional MOVE returns 412 on
  this provider. Use staged verification followed by guarded PUT and final full
  hash readback; reject destination collections. Never use unconditional MOVE
  to replace an existing Koofr path. Generic DAV retains conditional MOVE.
* Extend per-transfer deadline from five to thirty minutes for slow larger files;
  retain bounded streaming and network inactivity timeouts.
* Add native and real-browser regressions and deterministic valid PDF fixtures.
* Physical acceptance is tracked separately in docs/DASHBOARD.md.

## 0.2.1-dev — 2026-11-03

* Fix an end-of-scan panic: compute the schedule before entering the status
  spinlock. ESP-IDF libc time conversion can acquire a recursive environment lock
  and abort with interrupts disabled. Skip local-time conversion in idle mode.
* Add a production-source critical-section regression that rejects the original
  failing call and checks 16 short critical regions for known blocking operations.
* Version 0.2.0 uploaded/verified files while USB remained mounted but repeatedly
  restarted at pass completion. It is superseded and should not be installed.

## 0.2.0-dev — 2026-11-02

* Add dongle-only automatic backup while USB remains mounted; wait 120 seconds
  after the last USB write, abort an invalidated read session and remount afresh.
* Add overnight windows and time zones; keep eject-only mode as an optional choice.
* Stage uploads separately, verify contents, then publish with conditional MOVE
  and verify the final file. Refuse to overwrite a cloud collection.
* Close setup and resume backups without resetting USB; preserve saved credentials
  and schedule choices. Report idle/window/write-generation status in diagnostics.
* Add snapshot interruption, timing, schedule, namespace and DAV property tests.

## 0.1.6-dev — 2026-11-02

* Encode complete SHA-256 journal keys as lossless base32, keeping receipt names
  below the pinned SDK LittleFS 64-byte limit. Preserve legacy host-side receipts.
* Add a regression harness that enforces the device filename limit.

## 0.1.5-dev — 2026-11-02

* Add bounded USB status/check/test commands, persistent diagnostic events and
  a downloadable log without account credentials, SSIDs or user filenames.
* Add saved-connection checks and explicit synthetic upload/full-hash readback.
* Add the configured-device dashboard and secret-free reuse of saved cloud details.
* Verify real Wi-Fi, Koofr folder access and synthetic cloud upload/readback.

## 0.1.4-dev — 2026-11-02

* Use the owner's requested fixed setup credentials: Wi-Fi `admin123`, web
  username `admin`, web password `admin`. Keep the saved unique network name.
* Label Wi-Fi and web credentials separately on the screen. Setup still closes
  after ten minutes; the passwords do not expire or rotate on restart.
* Leave upstream Wi-Fi and cloud credentials unchanged. These known setup
  defaults are compiled into this personal build.

## 0.1.3-dev — 2026-11-02

* Guide phone setup through Wi-Fi, Koofr and Finish, with automatic network
  discovery, a prominent guest welcome/Agree link and optional advanced settings.
* Move the setup subnet when it conflicts with guest Wi-Fi; show the replacement
  address before moving, renew phone DHCP and remember the selected subnet.
* Retain the selected upstream connection across setup-address changes without
  sending its password back to the browser.
* Pass 50 core checks, 14 UI tests, 368 DNS/subnet assertions and 100,000 hostile
  packets; preview the real page at 320, 390 and 800 pixels with mocked networks.
* Flash and independently verify the application while preserving NVS byte for
  byte. Physical guest acceptance and cloud backups remain unverified.

## 0.1.2-dev — 2026-11-02

* Search for nearby Wi-Fi automatically and select a network from the list.
* Support explicitly selected open networks with no password.
* Add a temporary AP-to-station connection with bounded DNS/NAPT forwarding so
  the phone can open a guest network's sign-in page and accept its terms itself.
* Persist the setup AP/admin identity once in NVS, retaining it across normal
  restarts; keep request tokens fresh and stop guest forwarding at setup expiry.
* Explain HTTP request rejections without echoing private request data.
* Build and independently verify an app-only update on the original board;
  retain NVS, queue and SD contents. See the dated hardware report for limits.

## 0.1.1-dev — 2026-11-02

* Add guided phone setup, asynchronous Wi-Fi discovery, and Koofr address/CA presets.
* Add production-script UI tests and exact discovery API routes.
* Back up the original board flash, install/verify Pro firmware, and prepare the
  user's 64 GB FAT32 card under explicit authorization.

## 0.1.0-dev — 2026-10-27

* Audit full upstream source at 27eac0d; retain history, original README and attribution.
* Replace simultaneous host/FAT access with a mutex-serialized eject latch and a
  fresh, physically read-only firmware FAT mount. Reject host access until reboot.
* Remove cloud-to-local downloads and SD logging/configuration writes.
* Add upload-only SHA-256 change detection and two-generation CRC-protected internal
  flash receipts, with persistent pending/transferred/verified/error/retry states.
* Requeue acknowledged resources lost before verification (HTTP 404/410).
* Add exponential retry backoff, checked metadata writes and full remote hash/size
  readback; same-size modifications requeue and successful PUT alone is unverified.
* Require explicit-CA HTTPS with redirects disabled and configurable network time.
* Move bounded validated configuration to one internal NVS blob (avoiding the
  SDK string limit); do not claim at-rest encryption.
* Add random-password temporary setup/maintenance AP, bounded authenticated phone
  dashboard/configuration endpoint, CSRF checks and physical configuration reset.
* Keep the original ST7735 driver and add changed-row backup/error/status rendering.
* Pin a no-PSRAM original T-Dongle S3 build and explicit flash partition layout.
* Add sanitizer-backed portable production-code tests, parser fuzzing, synthetic
  loopback HTTP tests, baseline/final build measurements and development artifacts.

Version-specific physical acceptance and remaining site tests are recorded in
docs/DASHBOARD.md. This remains experimental development firmware.
