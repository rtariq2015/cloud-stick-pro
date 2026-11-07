# Historical source records

This folder holds preserved public source records. These are history, rather than
an inventory of the current checkout.

| Record | Meaning |
| --- | --- |
| [6 November source export](2026-11-06-source-export.json) | The earlier public export for source `e2de5a94c6df99f80a53e273056494627bbf06b1`; moved here from the repository root without changing its contents |

Preserved file SHA-256: `8a5399914c1e5126927eb54664454db7e4b43616b25a2d60410567b7eee5b839`.

Each new release source archive gets its own fresh `SOURCE_MANIFEST.json` at the
archive root. Use that release's record to check its files. The exporter reads
committed Git blobs and excludes private device data, recovery copies, keys and
build output. See [release records](../RELEASES.md) and [credits](../CREDITS.md).
