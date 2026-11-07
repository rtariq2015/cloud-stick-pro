# Repository review — 7 November 2026

This review makes the project easier to use and maintain. It follows the all-in-one
desktop work, rather than changing the embedded backup behavior or claiming a new
hardware test.

## What was reviewed

- The Git-tracked file tree, current branch, publication policy, source exporter,
  active workflows and release evidence.
- Firmware startup, USB/read-session, backup/journal, owner/Wi-Fi/cloud store and
  Bluetooth boundaries, with the existing source/test map.
- Desktop startup, strict bridge, normal transports, new independent installer,
  recovery lifecycle, packaging and first-account handoff.
- User-facing guides, version/download statements, screenshots, folder instructions
  and editor exclusions for generated output.

## Findings and changes

| Finding | Result |
| --- | --- |
| Top-level source manifest described an older export | Moved unchanged to `docs/provenance`, labelled as historical; release exports still generate their own fresh manifest |
| Generic header/library README templates did not explain this project | Replaced with project-specific Markdown indexes and links to the actual boundaries |
| An unused Windows workflow example missed the new firmware input and active checks | Removed the obsolete example and linked the real native workflow |
| Computer-app instructions mixed ordinary use, old versions and a machine-specific build path | Split everyday installation/setup from generic developer packaging instructions |
| Users had to search several technical guides to choose an app | Added a plain-language help index, troubleshooting guide and clear download/start steps |
| The new recovery file extension was not covered by the older ignore/export rules | Added `.csprec` exclusion and hard publication rejection for recovery artifacts, with regression tests |
| Reviewed editor configuration was missing from source exports | Added only `.vscode/settings.json`; unrelated machine-local editor files remain excluded |
| Large generated folders cluttered code navigation | Excluded build/cache/output folders from the saved editor view and searches without deleting their contents |
| First-account setup needed a clearer card-return handoff | Added the conditional microSD reminder before publishing Setup 0.6.0 |

## Verification and limits

The core native backup/WebDAV suite passed **70/70** using address/undefined
behavior sanitizers and a mock server. Publication/export-policy tests cover the
new exclusions and reviewed guide set. Local links and image files are checked.
The old source-manifest bytes retain SHA-256
`8a5399914c1e5126927eb54664454db7e4b43616b25a2d60410567b7eee5b839`.

A byte comparison verified **301 runtime/reference files** unchanged, plus the
build/profile/layout files. The organization work preserves firmware/app runtime sources, build profile paths,
partition layout, pinned SDK sources, protocol reference assets, notices and
previous releases. Generated/private output is hidden or excluded rather than
deleted. Unrelated staged editor files in the owner's canonical checkout remain
untouched.

This is a code/navigation and documentation review with the stated tests. It is
not a complete security audit, a new firmware build, physical dongle acceptance
or continuous background maintenance. Source/binary and hardware gates remain in
[Credits](CREDITS.md), [Maintenance](MAINTENANCE.md) and [Release records](RELEASES.md).
GitHub Pages remains disabled; the public-facing page is the repository README.
