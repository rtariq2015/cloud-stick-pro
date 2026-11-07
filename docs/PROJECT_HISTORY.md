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

## All-in-one Setup 0.6.0 and repository organization

Runtime/UI source `d6aeb6709a5b94e6a73eed4cc00b402000facd30` merged in
`a2219baba5f99712366d38bafc26d646aecadbdc` (PR #11). Native Windows/Linux
software checks and Windows per-user installation pass. The exact later package
sources/hashes are in the release manifest. Firmware stays 0.4.6-cache-dev from
`adc439bcc69802d289fa79b34f9994f4093d0f83`; Android stays 0.1.2. A new unconfigured
board and physical Windows USB/Bluetooth acceptance remain pending.

The follow-on organization adds a plain-language front page, help/troubleshooting,
folder indexes and current synthetic imagery. The older root source manifest is
preserved unchanged under [provenance](provenance/README.md). Product runtime
paths, SDK references, old artifacts and private-data boundaries are retained.
See the [0.6.0 report](releases/2026-11-07-integrated-setup.md) and
[repository review](REPOSITORY_REVIEW.md).

## Earlier Bluetooth cleanup and diagnostics preview

The [7 November preview](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/preview-2026.11.07-ble-cache)
records these component identities:

| Component / milestone | Exact source and scope |
| --- | --- |
| Timing-only firmware 0.4.5 | `aec31e7709dec9ee1a893a7cd871a109bb26a571`; real authenticated Save/Finish failed its memory proof; no binary download published |
| Firmware 0.4.6-cache-dev | `adc439bcc69802d289fa79b34f9994f4093d0f83`; protected normal install and two closes passed; separate service image passed cache stress and controlled two-network round trip |
| Desktop 0.5.7 | `90f632d7ee7d39be4a142aa7b842eddba68f57ab`; Diagnose my stick guidance, native Linux packaging/installed hashes/startup passed |
| Published source snapshot | `6d78684ffe741cb877c02a1567a83059b53a1f0e`; merged source, acceptance notes and eight hashed preview assets |

Saved revision 9 and all logical NVS values were retained across the encrypted
checkpoint comparison; the synthetic probe was removed. Backups resumed with
five verified files and zero errors. Exact personal authenticated phone Save/Finish,
actual AP outage, new exact-image upload/hash and packaged-GUI hardware acceptance
remain unverified. Read the [detailed report](releases/2026-11-06-ble-cleanup.md)
for the startup precondition failure, measurements and limits.

## Earlier Wi-Fi update — 0.4.4

Firmware **0.4.4-wifi-dev** comes from `d5d5f985e54b365b8441eb6e48392acf37288879`.
Desktop **0.5.4** comes from `eadf54f041dfd64e57f25972f3fc3181a0eb719c`. The
guarded build, two fresh encrypted recovery reads, independent app/protected-state
verification and installed native health passed. A setup-close cycle reconnected
Wi-Fi and resumed the worker. One new 2 MiB PDF passed automatic backup and an
independent full TLS cloud hash; final status showed five verified/zero errors.
Desktop archive/installed hashes and smoke passed.
Distinct-network physical failover was unvalidated on that image; see the
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
Development source and preview binaries are published with the unresolved
upstream license evidence and retained component notices. Publication does not
resolve the missing grant or replace binary packaging/acceptance gates; see
[Credits](CREDITS.md#upstream-license-evidence).
