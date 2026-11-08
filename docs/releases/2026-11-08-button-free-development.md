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
authentication mechanism. Production signature verification, pairing storage,
HTTP integration and the complete app flow still require implementation/testing.

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
