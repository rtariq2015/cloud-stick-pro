# Owner-authenticated Koofr setup: source candidate

Prepared 2026-11-04 in the isolated `feature/owner-cloud-setup` worktree from
`d6e9aa3`. This candidate closes the source-level onboarding gap for another
original T-Dongle-S3: after enrolling that board's own owner key and saving its
Wi-Fi profile, its owner can stage and save Koofr settings from the phone page.
This task did not contact a device or account, flash firmware, change live
credentials, publish a page, push GitHub changes or run a PlatformIO build.
The candidate is **not installed, released or physically accepted**.

## Boundaries and provider

`CSP_ENABLE_OWNER_CLOUD_SETUP` defaults to 0. Enabling it requires the managed
backup/private-owner Wi-Fi/BLE build; isolated Milestone 1 is rejected. Normal
production/default builds keep the old behavior. The existing Espressif
Security2 encrypted `csp-wifi` endpoint, request IDs, mailbox, session generation,
physical setup window and terminal rejection policy are reused. There is no
new encryption, HTTP write route, auth fallback, auto-migration or boot write.

The firmware constructs only
`https://app.koofr.net/dav/Koofr/<folder>/` with the existing pinned Koofr CA.
It accepts no arbitrary URL, CA, insecure-TLS option, redirect, schedule or
credential-testing command. Koofr documents the account email and a separate
application password for each WebDAV connection in its
[official WebDAV help](https://app.koofr.net/help/webdav) and
[application-password guidance](https://koofr.eu/help/koofr_with_webdav/which-password-to-use-when-connecting-via-webdav/).
Those public references were checked without any account authentication.

The existing backup worker retains its verified HTTPS/upload/readback behavior.
Saving cloud settings does not contact Koofr or establish access. After safe BLE
cleanup, ordinary worker operation must establish actual time/TLS/account
access and verified file receipts. Wrong or revoked credentials remain a real
failure; this interface cannot prove that a supplied app password is valid.

## Wire contract

Metadata advertises `csp.cloud:true,cloud_protocol:1` only in the enabled build.
All commands retain `v:2,id:<request-id>`; plaintext requests remain within 496
bytes and application results within 400 bytes. Revision/transaction values are
positive uint64 decimal strings; edits are uint32 values.

| Operation | Additional request fields | Successful result |
| --- | --- | --- |
| `cloud_status` | none | revision, configured/provider, field-presence booleans, editing/edit, store_fault |
| `cloud_begin` | revision | revision, transaction, edit 0, field-presence booleans |
| `cloud_stage` | transaction, exact current edit, field, value | same transaction, next edit, field-presence booleans |
| `cloud_commit` | transaction, revision, exact current edit, accept_unverified:true | durable:true, revision N+1, access_verified:false |
| `cloud_abort` | transaction | ok:true |

The only fields are `username`, `app_password` and `folder`. Login fields are
1–128 UTF-8 bytes without NUL/control/DEL; usernames also reject colons. Folder
names are 1–96 ASCII bytes in slash-separated segments matching
`[A-Za-z0-9][A-Za-z0-9._-]*`. Empty segments, traversal, leading/trailing slash,
query/percent characters and empty replacement fields are rejected. Duplicate,
unknown, mistyped or oversized wire fields fail parsing.

Status never returns the login, password, folder or endpoint text. Recognized
Koofr values may be explicitly retained without echoing them. Credentials from
another provider never populate a Koofr candidate. Edits are bound to the exact
owner/session, transaction, revision and edit counter, and expire after 120s or
the physical window, whichever ends first. A command rechecks the hard physical
deadline before storage even if the owner loop has not yet polled expiry.

Wi-Fi and cloud candidates cannot coexist. History reads and public-Wi-Fi
handoff/start are blocked while cloud edits are staged. Every write requires
maintenance mode and a completely stopped/reaped worker. Disconnect, lost
authentication, expiry or closing the window scrubs and releases the candidate.

## Atomic save and worker handoff

The new path retains the existing namespace `csp`, key `config`, schema 1,
`upload_only` mode, partition table, receipt journal format and SD behavior.
Missing `cloud_rev` is interpreted as revision 1 without rewriting anything.
The bounded 6144-byte legacy blob is cloned, preserving accepted unknown fields
and existing Wi-Fi/schedule settings. Unsupported/corrupt/deep/duplicate-key
legacy shapes fail closed; they are never silently normalized into a new record.
Missing legacy Wi-Fi fields for a new board are filled from its selected,
durably saved v2 profile for app-only recovery compatibility.

The save compares both the revision and the **entire original private blob**
immediately before one NVS value write/commit. It therefore detects legacy
changes even if another writer did not increment `cloud_rev`. Before writing,
the candidate must pass the actual backup worker's filtered decoder and its
unchanged validation/allocation budget. After the write, the full serialized
value must read back byte-for-byte before `durable:true` is returned.

Prewrite errors preserve the exact prior value. Once a write has been attempted,
a failed commit/readback has an **unknown outcome**. The code never guesses
unchanged/saved and never blindly rolls back. It scrubs staging, quarantines
worker/radio mutation, returns error 15 with `store_fault:true`, and keeps files
on USB. Its fault status is RAM-only. A later reviewed recovery/restart must
inspect the actual persisted state before proceeding.

After confirmed save, the owner-only RAM status invalidation requires the
worker to be Stopped, clears old counts/last-success state, and releases private
history cache. It does not modify journal receipts or SD files. The unchanged
worker destination fingerprint (`SHA256(server_url + "\\n" + username)`) prevents
receipts for a different destination being treated as current successes.
Phone cards also clear after a successful destination change.

Credentials/raw scratch are scrubbed before BLE shutdown/worker admission.
New NTP initialization is deferred until BLE is fully closed and memory recovery
is proven. Its public hostname has application-lifetime storage, preserving the
existing borrowed-pointer fix. Worker startup reloads the committed config and
retains all existing TLS/heap/USB safety gates.

## Memory and persistence limits

Optional cloud operations require at least 96KiB free internal heap and a 16KiB
contiguous block, in addition to all existing BLE/TLS gates. The fixed bounded
scratch maximum is 28,672 bytes: baseline 6144 + serialized 6144 + two 8192-byte
filtered-parser budgets. The first CAS buffer is released before parsing and
reallocated only after documents are destroyed. Candidate/profile/credential
objects, Arduino Strings, X509/SDK allocations and allocator overhead add to
that amount; target heap/largest-block/stack measurements remain mandatory.
This source refuses an operation with insufficient headroom rather than
relaxing the 32KiB active-BLE floor or any watchdog/TLS protection.

Native 64-bit measurements are CloudDraft 816, Record 400, Values 355,
WireRequest 280 and StoredNetworks 856 bytes. These are desktop object sizes,
not an ESP32 linker/RAM report. The feature adds only small flags/pointers and
bounded public status globally; the large private draft/raw data exist only
during an owner window and are released before worker resumption.

Native tests configure ArduinoJson pool capacity 128 slots consistently across
all translation units, matching the pinned ESP32 slot count. Native slots are
larger, so this is conservative per-pool storage; it is not an SDK production
change. A separate default-native 256-slot configuration demonstrates allocation
refusal before NVS write under the unchanged 8192-byte parser budget. No warning
or sanitizer diagnostic is suppressed.

The existing config blob still has the existing flash-at-rest protection;
this task adds no encrypted-NVS/eFuse/key provisioning. Raw internal-flash/NVS
copies therefore remain private recovery material. Write-only API responses
and encrypted BLE transport do not make a raw flash backup public-safe.

## Desktop evidence and remaining gate

Commands below run inside the existing compose environment from this worktree:

```sh
python3 test/cloud_setup/native.py
python3 test/cloud_setup/store_tests.py
python3 test/cloud_setup/runtime_native.py
python3 tools/test_backup_runtime.py
node --test test/cloud_setup/phone.test.mjs
```

Use the documented compose wrapper and pinned Python/compiler/dependency paths;
the runners never download packages or open devices. Evidence is under
`artifacts/owner-cloud-native/`, `artifacts/owner-cloud-store/`,
`artifacts/owner-cloud-runtime/` and `artifacts/backup-wifi-integration/runtime/`.
The phone evidence and its separate broad 535-check run are described in
[public project history](PROJECT_HISTORY.md).

The model/wire suite passed 6 build/run records, and the actual owner-loop
suite passed its build plus 15 scenarios. The unchanged feature-disabled
combined runtime passed 45 records. Storage passed 6 build/run records covering
17 storage/fault groups, one explicit default-native allocation refusal and
9 existing feature-disabled loader groups. All three new native manifests are
complete and source-stable; source hashes must still be rechecked at integration.
All native acceptance requires empty stderr, fatal ASan/UBSan and leak checking;
failed harness attempts remain archived and are not counted as acceptance.
Simulated components are BLE admission, USB, Wi-Fi, workers and NVS faults;
actual production runtime, mailbox, wire parser, NVS adapter, config validators
and storage model are exercised. This is not physical Security2/cloud acceptance.

Required before release: an image-specific enabled/default-disabled build and
linker comparison; actual SDK Security2 cloud interoperation; physical Android
and heap/stack/cleanup tests; saved-config power-loss/readback checks with only
synthetic accounts/files; fresh private recovery material; controlled cloud
authentication/upload/full-readback; and another identical board's initial
owner/Wi-Fi/Koofr onboarding. A one-folder source package alone proves none of
those. No operational flasher or factory install instruction is added here.
