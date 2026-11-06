# Paused-session file receipt prototype

This is a source-only prototype based on `bffdb7f0ceda63dc65a54e998e466f1c5f2ed0ab`,
on `feature/receipt-history`. It has not been compiled for the ESP32, installed,
published, or tested with physical storage or Bluetooth. The normal feature
default is `CSP_ENABLE_FILE_HISTORY=0`; no PlatformIO environment enables it.

The optional phone card shows **up to eight saved receipt summaries**, not all
128 files and not a chronological upload audit. Pending/transferred records are
prioritized by their saved retry schedule, followed by newest verification time.
The destination identity and watched-folder prefix must match. Files without a
saved receipt are absent. Each row describes the specific version represented
by that receipt; it is not evidence that a file subsequently edited by the host
has been uploaded. Full content verification remains the existing receipt's
`Verified` state, and a transferred/unverified copy stays distinct.

## Lifetime and memory

`FileHistoryRetirementScope` is declared after `WorkerMetadataScope` and before
the normal journal, remote, engine and event guard. Reverse destruction closes
all normal engine/TLS/source scopes and flushes existing event metadata first.
Only a requested retirement with a successful metadata mount captures the view;
the worker still exclusively owns that mount. The capture uses actual
`FileJournal::next`, which validates the existing dual-generation CRC records.
It does not save a receipt, change its codec, mount or format a filesystem, read
or write microSD, or start networking. Each receipt iteration yields on Arduino
after the existing Journal has closed its handles.

Eight fixed rows use exactly **2,240 allocated bytes**. No per-path allocation
survives capture; temporary Journal strings remain bounded by the existing
record/path limits. Pointer, packed counters and lock have a compile-time
**64-byte resident-control ceiling** (64 bytes in the native mutex build).
Target RAM, stack and linker deltas still require a separate firmware build.
There is no 128-path resident index.

An admitted worker restart first detaches, overwrites and frees the complete
cache, before allocating its stack, starting its task, or reaching TLS. Fragment
reads and detach use a short lock; erase/free occurs outside it. Revision changes
reject an old cursor and prevent a cancelled capture from publishing afterward.
The publisher checks the exhaustion latch as well as the revision, because a
release at UINT32_MAX cannot advance the revision further. Revision exhaustion
blocks the optional view instead of wrapping. Allocation,
read and corrupt-record failures are exposed as flags. Exactly 128 examined
identities conservatively marks the listing incomplete without an extra probe.

The fixed two-minute BLE session is unchanged. At most 33 history commands are
needed for eight 240-byte paths, in addition to existing setup reads. Physical
latency and the bounded retirement listing's worst duration remain to be
measured; no watchdog or timeout has been weakened. Public Wi-Fi handoff also
clears the cache after verified BLE cleanup and worker reclamation, before
routing or its independent TLS probe. This path does not rely on a later backup
worker restart to release memory.

## Authenticated protocol

The optional version capability is `file_history: true` with
`file_history_protocol: 1`. It does not change the existing authenticated Info
shape, GATT UUIDs, official Security 2 handshake, credentials or owner policy.
Old devices do not receive history commands.

* `history_status` accepts no additional arguments and returns `ok`, revision
  `g`, row count `n`, matching-record count `matched`, decimal-string capture time
  `when`, and boolean `loaded/complete/limited/corrupt/io_failed/allocation_failed/exhausted`.
* `history_row` accepts revision `g` (positive uint32), row `i` (0..7), byte offset
  `o` (0..240). It returns `ok/g/i/n`, decimal strings `b` (bytes), `v` (verified
  Unix time), `r` (retry Unix time), state `s` (0/1/2), signed error `e`, attempts
  `a`, path byte count `p`, offset `o`, next `x`, boolean more `m`, and `h`.
  `h` contains up to 64 original path bytes encoded as lowercase hex. Hex is a
  byte representation, not an encryption protocol. The phone assembles the full
  byte path before strict UTF-8 decoding, so multi-byte characters can cross a
  fragment boundary without damage. Uint64 values remain exact decimal strings.

Only an existing authenticated owner session, active maintenance, and worker
`Stopped` may read. The mailbox still revokes queued work when authorization
ends. No BLE command opens LittleFS or SD. Runtime result/envelope limits remain
400/496 bytes. Actual runtime tests with maximum-width numeric fields and
240-byte quote paths observed 315-byte results and 373-byte envelopes.

Phone validation rejects unknown/missing fields, mixed revisions, changed row
metadata, oversized/invalid fragments, invalid UTF-8, unsafe paths, and stale
responses. Rendering uses `textContent`; page hide/freeze/restore, disconnect,
revocation and late async work cannot retain the authenticated file card.
Filenames never enter generic logs, error reflection, browser storage or network
uploads from this feature.

## Native verification

Run inside the existing compose development container:

```text
python3 test/file_history/run.py
python3 test/file_history/run_phone.py
```

`run.py` builds actual cache/Journal/core, wire parser, owner runtime/NVS, and
actual worker source with strict nonrecovering ASan/UBSan and leak detection.
The worker's SDK/radio/USB/Journal/TLS interfaces are controlled adapters; a
narrow history callback verifies real scope ordering. Actual Journal/cache
behavior is separately linked against synthetic POSIX directories, with injected
allocation/read/cancellation faults and full before/after receipt-byte checks.
The runtime test forbids any `fopen` during BLE commands. Default/off/without-
backup wire variants, invalid feature flags and symbol-free default-off object
are checked. It does not claim physical RTOS, GATT, USB or TLS acceptance.

`run_phone.py` tests the actual ManagementConnection allowlist, app and parser
with synthetic DOM/GATT/transparent test security. It is not a cryptographic
interoperability test. Machine evidence and hashes are in
`artifacts/file-history/results.json` and
`artifacts/file-history-phone/results.json`.

Existing regression evidence is also retained in `artifacts/event-log/`,
`artifacts/event-log-phone/`, `artifacts/public-wifi-assistance/` and
`artifacts/backup-wifi-integration/dav-cancel/`.
No SDK, partition CSV, USB implementation, storage safety model, receipt format,
cloud adapter, production config, or other worktree was changed by this task.

Before integration, review the final diff and build default-off and explicitly
enabled images with the exact frozen SDK. Check linker/static/stack evidence,
metadata size, and compatibility with any concurrent parent changes. Physical
acceptance must use synthetic/sacrificial files, measure retirement duration,
repeat BLE-to-TLS cycles, confirm cache release and allocator margin, compare USB
descriptors and protected state, and independently verify unchanged receipts.
This prototype is not authorization to flash or publish it.
