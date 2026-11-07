# Public project history

This is the curated, ordered development index. It records source identities and
scope without live device identifiers, private paths, account details or raw
operator/recovery logs. The complete working records remain private and unchanged.
Firmware, desktop and Android versions are separate.

## Ordered milestones

| Date | Component / milestone | Exact source identity and scope |
| --- | --- | --- |
| Upstream baseline | Original Cloud Stick by John / JMcrafter26 | `27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d`; retained origin, README assertion and acknowledgements |
| 2026-11-03 | Pro 0.3.3 backup baseline | `9d222a13c1397b0944167c46d0f3dc9efc316325`; earlier upload/readback/recovery evidence belongs to this revision |
| 2026-11-03 | Initial BLE source prototype | `d541c13cfddd400486409ce375dc0215f7e693cd`; synthetic Security 2 prototype, subsequently corrected |
| 2026-11-03 | BLE corrective milestone | `b2f2a19fb52dc110c1b74c9ff3da457cef1c45f6`; lifecycle/security/isolation corrections; desktop gates did not themselves establish physical readiness |
| 2026-11-03 | Test-helper stream handling | `9e49c0b741e8653df362386f8b7f4595220d14ce`; subprocess output must drain before a cleanup verdict |
| 2026-11-05 | Combined RC5 firmware | `6bb5082a36b84b8451875d0fa7201f0d95192e52`; earlier current-board backup/health candidate with bounded acceptance |
| 2026-11-05 | Guided setup firmware 0.4.1 | `c6ec0e0e7e3ffde43a1d7de37808a1729fa06f8c`; authenticated inactivity-based setup sessions; preserved installed firmware baseline |
| 2026-11-06 | Desktop 0.5.0 | `c76d1ccc3fce60c66c4cb09d97897fad8caa48b8`; explicit read-only service health workspace |
| 2026-11-06 | Android 0.1.0 | `0bf3d960f2a83a7d6f72ecf3a44d5c4527deba28`; bundled local authenticated viewer; building/signing did not establish real phone acceptance |
| 2026-11-06 | Combined source checkpoint | `d4ab3bb7bcb16f56f385ff275f36fe27dc6773a9`; component delivery/evidence index |
| 2026-11-06 | Desktop 0.5.1 / Android 0.1.1 | `4fc282cb326a1bfb783f059beaa7561d354226f8`; clearer summaries, lazy optional records and lifecycle presentation |
| 2026-11-06 | Incremental firmware candidate | `d0bb28ae2e2db328708c8c18684b25cec8d98337`; receipt-bound content proofs, bounded USB-write coverage and LCD/LAN wording |
| 2026-11-06 | Final candidate build source | `cee8a8e64d21917b1640b3a5d782988305623dd8`; cache-disabled worker include isolation and regression registration |
| 2026-11-06 | Desktop 0.5.2 firmware admission | `82e5d97282d52da6436a990dcf2d6768110b34e6`; admit exact reviewed 0.4.2 firmware without accepting arbitrary versions |
| 2026-11-06 | Modern companions and LCD | `c319dadbbe730a96383219c8e39973419d65b028`; desktop 0.5.3, Android 0.1.2 and firmware 0.4.3-ui-dev; unchanged ownership/backup runtime |

## Current Wi-Fi update

Firmware **0.4.4-wifi-dev** comes from `d5d5f985e54b365b8441eb6e48392acf37288879`.
Desktop **0.5.4** comes from `eadf54f041dfd64e57f25972f3fc3181a0eb719c`. The
guarded build, two fresh encrypted recovery reads, independent app/protected-state
verification and installed native health passed. A setup-close cycle reconnected
Wi-Fi and resumed the worker. One new 2 MiB PDF passed automatic backup and an
independent full TLS cloud hash; final status showed five verified/zero errors.
Desktop archive/installed hashes and smoke passed.
Distinct-network physical failover remains unvalidated; see the
[Wi-Fi update report](releases/2026-11-06-wifi-fallback.md).

## Earlier installed acceptance (0.4.2 / 0.4.3)

Firmware **0.4.2-incremental-dev** is installed after the reviewed app-only
update, two matching fresh encrypted/reopened recovery reads and independent
app/protected-state verification. The preserved0.4.1 app remains recovery material.
Two separately added 2 MiB PDFs passed automatic upload and independent full cloud
hashes. A 405-second quiet observation and one BLE lifecycle cycle passed. This
is not a completed 100-file physical acceptance or throughput improvement claim.
Pre-ROM operator failures were retained;
their Git-path/service-working-directory corrections did not weaken the gates.

Linux desktop **0.5.3** was installed and its 3,008 regular-file hashes and
installed executable's synthetic smoke were checked. This is application
installation evidence, not acceptance of a real Bluetooth session in the new UI.
Android **0.1.2** was built/signed and checked; exact-version phone installation,
permissions, BLE and resume acceptance remain separate work.

Modern firmware **0.4.3-ui-dev** is installed after protected verification. A new
automatic 2 MiB PDF/full cloud hash and native USB health passed. It adds 464 app
bytes and no static RAM against 0.4.2. Packaged-GUI interaction, exact-version
Android, Windows and actual venue acceptance remain separate gates.

For candidate hashes, measured resource changes and the exact distinction
between synthetic tests and physical work, see the
[2026-11-06 improvement report](releases/2026-11-06-improvements.md).
The 100-verified-plus-two-files fixture is a desktop model/adapter result, not a
measurement of ESP32 throughput or an unlimited queue.

## Reading public releases

Use the [release guide](RELEASES.md), [getting-started guide](GETTING_STARTED.md)
and [project layout](PROJECT_LAYOUT.md). Public source exports include reviewed
guides and this index, not raw operator handoffs, enrollment/recovery dossiers,
private backups or the original working README with machine-specific details.
Original notices and component licenses remain with the source.

ELF/map debug products and private operator evidence remain developer-local;
they are not silently rewritten into a supposedly identical public artifact.
App images and their pinned identities are unchanged by documentation curation.
The upstream license-text gap still blocks publishing firmware/application
source or binaries; see [Credits](CREDITS.md#upstream-license-evidence).
