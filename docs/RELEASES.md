# Releases and revision records

Firmware, desktop and Android have separate version numbers. Installing a desktop
or phone update does not install firmware on the USB stick.

| Component | Preserved / installed evidence | Latest local revision |
| --- | --- | --- |
| Firmware | 0.4.1/0.4.2/0.4.3 recovery apps preserved | 0.4.4-wifi-dev installed; protected app update, Wi-Fi recovery and new PDF/full cloud hash passed |
| Linux desktop | Prior versions preserved | 0.5.4 installed; archives, installed hashes and smoke passed; source-native physical diagnostics passed; packaged-GUI interaction unvalidated |
| Android viewer | 0.1.0 phone baseline; earlier APKs preserved | 0.1.2, version code 3, signed/verified; exact-version phone acceptance pending |
| Windows desktop | Shared source/build recipe | 0.5.4 recipe updated; native Windows binary/acceptance pending |

The new source work starts from combined suite commit
`d4ab3bb7bcb16f56f385ff275f36fe27dc6773a9`. The original project remains credited
to [John / JMcrafter26](https://github.com/JMcrafter26/cloud-stick), baseline
`27eac0d0f8ceab714cdf7640d50f4c4d57ed7f3d`.

## 2026-11-06 improvement round

The focused change is to avoid re-reading every verified file's contents when a
different file is added. This requires bounded evidence of which physical USB
sectors changed, a fresh file-layout check and an exact existing verified receipt.
File names, lengths and timestamps alone do not establish that a file is unchanged.
Missing evidence always selects the original full-content check.

The modern desktop and Android apps use a blue/white layout. Desktop has a
numbered setup rail; Android prioritizes recorded counts and recent files. The
modern LCD uses a navy palette, compact indicators and explicit activity badges.
It and the local dashboard distinguish finding files, checking local content,
uploading and cloud readback. Retained transfer fields do not become ongoing work
once an operation is inactive. Partial results remain partial.

Desktop and Android summaries distinguish a new local status read from a
previously recorded backup result. The apps avoid redundant initial reads and
defer optional records until the user opens them. Finishing waits for the device
acknowledgment; a close acknowledgment alone is not proof of a new cloud upload.

Installed 0.4.2 automatically backed up two separate 2 MiB synthetic PDFs;
independent full cloud downloads matched both original SHA-256 hashes. One BLE
lifecycle cycle and a 405-second quiet observation passed on the same boot/USB
enumeration. The native 100-file fixture skipped old-content hashing; a physical
100-file workload and old-content-read counter remain unvalidated. Raw upload
rate remains approximately 50–54 KiB/s; no new raw-speed improvement is claimed.

Exact build hashes, resource comparisons, tests and remaining acceptance gates
are in the version-specific report. A screenshot, source test or successful
compilation must not be labelled physical acceptance of a new image.

Current evidence: [2026-11-06 improvement report](releases/2026-11-06-improvements.md).

## Ordered history

* [Saved Wi-Fi fallback update](releases/2026-11-06-wifi-fallback.md):
  `0.4.4-wifi-dev` and desktop `0.5.4`, guarded build, protected app-only update,
  native health and setup-close Wi-Fi recovery passed. Distinct-network
  switching remains a separate physical acceptance gate.

* [Changelog](../CHANGELOG.md): user-visible changes, newest first.
* [Public project history](PROJECT_HISTORY.md): ordered, sanitized component milestones and exact source identities.
* Raw operator logs, recovery/enrollment dossiers and machine history remain private and unchanged.
* [Project layout](PROJECT_LAYOUT.md): where current source and supporting material live.

Historical reports describe their original image and test scope. For example,
an early BLE-only image intentionally disabled backups; that is not the behavior
of the later combined firmware. Do not apply an old image's test verdict to a
new candidate merely because its version number is similar.

## Package layout

The source exporter assembles one `cloud-stick-pro/` folder. Firmware source stays
at the root, desktop code stays in `desktop/`, and Android stays in `android/`.
Documentation uses an explicit reviewed allowlist: current public guides,
component notices, named synthetic previews and the curated history index.
Raw operator/recovery/handoff/history dossiers, the original working README
and raw machine-history JSON are excluded rather than automatically moved.
The exporter reads committed Git blobs, not the live working directory, and emits
`SOURCE_MANIFEST.json` with an SHA-256 for every exported file. Git history, local
device data, build caches, signing material and recovery vaults are excluded.
Uncommitted documentation is not captured: explicitly review and identify a
documentation revision or separately manifested overlay before packaging it.
Debug ELF/map products remain developer-local, outside the public source
package; app images and pinned binary identities are not changed by this curation.
The reviewed source snapshot is published with its manifest and retained license
evidence. Binary releases have separate packaging, signing and acceptance gates.

```sh
python3 tools/export_public_project.py --revision HEAD \
  --output ../cloud-stick-pro \
  --archive ../cloud-stick-pro-source.zip
```

This command creates a local review package only. It performs no upload, device
operation, install, erase or format. It refuses existing output rather than
replacing it. Publication is a separate action; preserve the unresolved upstream
license evidence described in [Credits](CREDITS.md).
