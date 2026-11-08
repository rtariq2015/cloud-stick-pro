# Button-free connection development — 8 November 2026

This work is in progress. **It does not yet let the installed phone app open
Bluetooth without BOOT.** Viewer 0.2.1 and normal firmware 0.4.6-cache-dev remain
the offered versions. The intermittent Bluetooth memory-recovery failure in
[issue #5](https://github.com/rtariq2015/cloud-stick-pro/issues/5) remains open.

The intended flow pairs a trusted phone during an authenticated Bluetooth session.
Later, the phone signs a fresh Wi-Fi challenge to request a finite Bluetooth
window, then uses the existing personal login for private history/settings.
Passwords are not sent over the plain local dashboard connection. The dashboard
remains a public status view. Unpaired phones retain the physical-button path.

## First source milestone

A default-off wake policy binds the pairing record to the exact board and owner
record revision. It checks expiry, single-use nonces, request cooldowns and busy
admission before accepting a proof. Changing the owner record invalidates the
old pairing. A fixed, domain-separated signature transcript keeps the nonce and
paired identity together. CRC detects a corrupt stored record; it is not an
authentication mechanism. These first tests used simulated proof verification. The later runtime milestone
below adds the actual verifier, storage, HTTP and Android flow; physical feature
acceptance remains outstanding.

Native ASan/UBSan tests cover corrupt records, other board/owner revision,
replay, expiry, forged proof, busy state, cooldown and clock rollover. Their
proof verifier and RNG are simulated; these tests are not cryptographic or
physical acceptance. A new CI job runs the same bounded native tests.

A separate **0.4.7-release-lab** private diagnostic profile observes small
allocation addresses, sizes and return addresses, plus public heap metadata,
to investigate cleanup. It never reads allocation payloads or performs a reset,
NVS mutation, file operation or lifecycle override. It keeps all existing stop,
worker, memory, storage, authentication, TLS and watchdog gates. The normal and
historical profiles reject its feature/identity flags and omit its commands.
The observer uses fixed storage allocated at boot; native tests use invalid
synthetic payload addresses to check metadata-only behavior. Target latency and
cache-off behavior still need hardware review.

The guarded private build retains the frozen 6,509-entry SDK manifest
`1667e4e5650d161240a3ba6a271f02004717ddc1caec56cf178805f8e0738237`,
unchanged partition layout and reviewed USB/storage sources. SDK inputs stay
read-only. The build now puts PlatformIO’s disposable cache inside the project;
it does not write to the frozen core’s cache. Retained TFT_eSPI 2.5.43 and
ArduinoJson 7.4.3 dependencies support the offline build. There is no new
public firmware binary, physical install, resolved cleanup claim or always-on
Bluetooth claim in this milestone.

Original attribution, dependency notices and the source/binary gate remain in
[Credits](../CREDITS.md). Keys, recovery checkpoints, credentials, personal
identifiers, raw diagnostic output and the signed test helper are not source or
release inputs. The native observer binary stays private.

## Runtime and app development milestone

The development branch now contains firmware **0.4.7-wake-dev** and Android
**0.3.0-dev, code 6**. These are candidates, not offered public updates. They have
not been installed together on the physical phone and stick.

On the first connection, use the existing BOOT window and personal sign-in, then
choose **Trust this phone**. This stores one phone's public signing key on the
stick. Afterward, on reachable Wi-Fi, signing in asks the trusted stick to open
Bluetooth and waits for that exact previously paired device. The personal login
is still checked over encrypted Bluetooth. The app includes an explicit BOOT
fallback for a first connection or unavailable Wi-Fi. Trusting another phone
replaces the previous phone; changing the owner login invalidates old trust.

Android keeps the private P-256 signing key in AndroidKeyStore and remembers only
public pairing bindings and the existing private IPv4 address. A fixed native
reader obtains a short-lived nonce on port 81 and signs a domain-separated
66-byte message. The stick checks the signature with mbedTLS, freshness, owner
revision, cooldown and current eligibility. Port 80 remains the public read-only
dashboard. No password goes over HTTP, and a wake proof does not grant a private
management session. The renderer still cannot load remote pages or issue network
requests. The new listener exists only on the connected station interface, admits
one bounded client, and finishes the response/peer close before requesting the
existing worker-pause/Bluetooth lifecycle. Uncertain cleanup keeps its safeguard.

Verified computer tests:

- 201 combined desktop/Android JavaScript tests, including trusted-ID selection,
  explicit fallback, cancellation, stale results and pairing acknowledgement.
- 60 JVM tests, release build and Android lint. Java JCA signatures are checked
  against the actual production mbedTLS verifier in native ASan/UBSan tests.
- Actual loopback TCP tests exercise the production bounded server, signed proof,
  replay denial, peer-close handoff and incomplete-close expiry. NVS, hardware
  admission, RNG and radio are simulated there.
- 60 synthetic render checks at two phone widths, including the new trust action.
  No device or authentication is involved in those visual fixtures.
- Seven checks on the signed candidate in the real Android 16 emulator verify
  AndroidKeyStore creation, non-exportable private key, key reuse, saved public
  binding, a real signature, unknown-binding rejection and hidden pre-login trust
  controls. This is emulator evidence, not physical-phone acceptance.
- The distinct firmware profile builds with all 6,509 frozen SDK entries verified
  before and after, unchanged partitions and the existing USB/storage source gate.
  Build-profile isolation has 59 passing self-tests. Historical images reject the
  new wake flags and the new profile excludes private observer commands.

The signed emulator candidate APK is 2,809,653 bytes, SHA-256
`e391cc259c58602bd3809c7a207e94e91d2075d073eed5e31321d0857a32687b`.
Its signing certificate matches the existing official app. Neither this candidate
nor the signed privileged test helper is a public release asset.

Pending: exact firmware/app installation, first trust and reconnection without
BOOT, unavailable Wi-Fi fallback, password-change revocation, actual connection
cleanup and endurance on the physical pair. The earlier intermittent memory
recovery failure remains open. Do not claim always-on Bluetooth, a completed
button-free acceptance test or automatic firmware entry into download mode.

## Separate physical diagnostic evidence

The private observer image was installed through the corrected Setup 0.6.1 engine,
with two fresh matching encrypted full recovery reads, reopened verification,
app-only writing and complete flash-preservation/readback checks. See the separate
[installer correction](2026-11-08-desktop-rom-geometry.md). This was a protected
update of the existing owned board, not an unconfigured-board or Windows USB test.

Two subsequent real-stick closes passed: an unauthenticated window and a Linux
Bluetooth session with normal personal authentication and bounded private-history
reads. Memory recovered, Wi-Fi and backups resumed, five verified records stayed
unchanged, and no reset occurred. These did not reproduce or explain the prior
intermittent failure. Metadata traces and private read results remain outside Git.

**BOOT for everyday Bluetooth and BOOT for firmware installation are separate.**
The new connection flow is intended to remove BOOT after first trust when Wi-Fi
is reachable. The current protected firmware installer still requires manual ROM
entry after safe ejection. Automatic firmware download-mode entry is not included.

Candidate firmware bytes and all reviewed source hashes are recorded in the
[source identity record](2026-11-08-button-free-source-review.json). These hashes
identify development artifacts; they do not establish physical acceptance.
