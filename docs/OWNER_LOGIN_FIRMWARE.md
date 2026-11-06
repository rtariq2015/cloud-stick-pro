# Owner login rotation: source prototype

Prepared 2026-11-05 on `feature/owner-login-firmware`, starting at the frozen
owner-cloud commit `7e2231181aec1fa8b7967cc56af9f06c15c5df4b`.
This is source/desktop evidence. No device, live account, persistent user
configuration, microSD, website, firmware flash, security setting or eFuse was
accessed or changed. No PlatformIO build was run in this task.

## Behavior and scope

An already authenticated owner can replace the Bluetooth login verifier during
the existing physical setup window. The phone derives a canonical COW2 record
locally from its chosen username/password; only that verifier record crosses
the existing Security2 encrypted transport. Firmware never receives or stores
the new plaintext password or username. The pinned SDK accepts the username
provided by the client during SRP; there is no fixed server-side username to
rewrite and no NVS schema migration.

`CSP_ENABLE_OWNER_LOGIN` defaults to 0 and requires the combined managed private
owner Wi-Fi/BLE/backup build. It rejects isolated Milestone 1. The normal
production build remains unchanged. This adds no unauthenticated endpoint,
password fallback, reset action, HTTP configuration route or remote backend.
Existing Wi-Fi, Koofr settings, backup identity/fingerprints, receipt journal,
partitions, USB safety policy and SD files are retained.

Metadata advertises `owner_login:true,owner_login_protocol:1` only when enabled.
Existing outer request IDs, v2 framing, job mailbox and `result.data` placement
are unchanged. The new operations are:

| Operation | Fields besides v/id/op | Result |
| --- | --- | --- |
| `login_begin` | none | transaction/revision/device; edit 0, received 0 |
| `login_stage` | transaction, exact edit, contiguous offset, data | next edit and exact received-byte count |
| `login_commit` | transaction, revision, exact edit, accept:true | new revision; durable/reconnect_required true |
| `login_abort` | transaction | same transaction/revision; edit 0, received 0 |

Every success has exactly `ok:true,error:0,transaction,revision,edit,received,
device,durable,reconnect_required,store_fault:false`. `device` is the 12-character
lowercase public device MAC. `data` is 1–128 decoded bytes encoded as 2–256 hex
characters. Four normal chunks are 128/128/128/48 bytes. Empty, oversized,
nonhex, duplicate, unknown, mistyped, out-of-order, overlapping, replayed-edit
and stale-session input is refused. Plaintext requests stay within 496 bytes
and application results within 400 bytes.

Revisions are positive uint64 decimal fingerprints: SHA256 of the complete 432
bytes, first 8 digest bytes as big-endian uint64; a successful all-zero prefix
maps to 1. Hash-library failure is never a revision. Commit must produce a
different fingerprint, not old+1. Full private byte comparisons still decide
CAS; a short fingerprint never substitutes for comparing the complete value.

## Save and old-session barrier

The candidate expires after 120s or the existing physical window, whichever
ends first. Commands recheck the deadline before writing, including when the
owner loop has not yet polled expiry. Mutations require the exact authenticated
owner/session plus maintenance and a completely stopped/reaped backup worker.
Wi-Fi/cloud candidates and public-Wi-Fi handoffs cannot run concurrently;
history commands are blocked while login editing is active. Read-only status
may continue until commit starts.

The existing COW2 decoder validates exact 432-byte length, device MAC, schema,
reserved bytes, canonical nonzero salt/verifier, RFC5054 group bound and CRC.
CRC checks corruption and is not authorization. Storage reads and compares the
entire current `csp-owner/owner` value, validates the new record, rechecks
authorization, arms the application barrier, writes one 432-byte NVS value,
commits and privately reads back every byte. Missing/corrupt owner records
cannot be silently replaced through this API.

An explicit prewrite hook arms the barrier **before** the NVS linearization
point, closing the gap between commit and callback admission. The barrier
allows only the exact commit-result job in the same Security2 session. It is
enforced at callback admission, queued-job execution, late mailbox publication,
Info return and SDK encryption. Already queued old-session settings/history/
public commands cannot execute or publish a successful response afterward.

A valid blocked request receives a bounded encrypted reconnect-required error;
it does not terminally reject the SDK session and suppress the commit ACK.
Malformed/auth-invalid frames retain the existing terminal rejection policy.
Late non-ACK plaintext is sanitized before SDK encryption. The publication
lease serializes this bounded encryption with prewrite barrier arming, while
`stateMutex` is never held across the SDK call. The lease never wraps NVS,
network activity or teardown. Crypto packets already admitted under the lease
complete before barrier arming. Target scheduling/watchdog acceptance is pending.

The old SDK salt/verifier buffers remain unchanged through ACK encryption.
Only a byte-matched exact commit-result response can mark the ACK encrypted;
merely allocating/returning its plaintext does not. The first ACK starts a 1s
settlement period; repeated polls cannot extend it. Client disconnect also
permits close. A finite 5s no-ACK wait closes a stalled session; if the client
never received the result, its UI must report an unknown outcome and reconnect
with the new login rather than retrying the old commit.

The owner task then uses the existing verified BLE stop, radio/memory recovery
and worker gates. It reloads and verifies the new record only after SDK teardown,
checks the committed fingerprint and post-reload headroom, then replaces the
old parameter buffers and resumes the same backup configuration. Stop, recovery,
reload or memory failure retains a blocked state and never forces a USB reset.

Any failed write/commit/exact readback after a write attempt is **unknown**.
Error 15 with `store_fault:true` is returned through the same protected result
ticket if possible. The code does not claim unchanged/saved, blindly restore
the old record or resume workers. Cleanup retains the barrier; a reviewed
private recovery inspection must establish the actual persisted owner record.

## Memory and recovery limits

The optional operations require at least 48 KiB free internal heap and a 16 KiB
contiguous block, preserving the existing 160 KiB BLE-start/32 KiB active floors
and watchdog/TLS protections. Native storage has no dynamic buffers: Bytes 432,
Current 440, Store 16, NVS adapter 8 bytes. One 432-byte scratch is reused for CAS
and readback; decoded verifier and digest temporaries are scrubbed. The dynamic
draft contains the expected 432-byte value and candidate 432 bytes and is erased
on abort, expiry, authentication loss or commit. Native Candidate is 472 bytes,
Handoff 40, dynamic LoginDraft 928, and the login-only WireRequest is 280 bytes.
These are desktop ABI measurements; each scenario logs its provenance. ESP32
linker size, actual SDK allocation overhead, worst
stack use and heap/largest-block recovery remain target-test prerequisites.

The owner record contains a verifier, not a reversible password. There is no
email service, backend recovery or way to read a forgotten username/password
from this format. Keep the new login and private owner/recovery material offline.
The existing physical first-enrollment action deliberately refuses an existing
record. A forgotten login requires an explicitly scoped, private USB recovery
procedure using a known verified owner backup or a separately reviewed owner-only
re-enrollment operation. Such a recovery tool is not implemented by this change;
it must not erase Wi-Fi, Koofr, receipts, files or the SD card as a side effect.
No plaintext fallback or automatic erase is provided.

## Desktop tests and release gate

Use the existing compose wrapper and pinned Python/dependencies in this worktree:

```sh
python3 test/owner_login/native.py
python3 test/owner_login/store_tests.py
python3 test/owner_login/security2_tests.py
python3 test/owner_login/runtime_native.py
python3 test/owner_login/adapter_tests.py
```

The model/wire tests cover staging, consent, expiry/rollover, MAC validation,
handoff timing and disabled-feature rejection. Storage tests exercise the actual
decoder/SHA256/NVS adapter and full-CAS/hook/write/commit/readback failure matrix.
The trusted synthetic SDK host commits through the actual Store, selects the new
parameters, authenticates a custom operator username/password with the production
Security2Client, rejects old/wrong credentials/stale ciphertext and repeats 3
encrypted reconnects. It is not the production application authorization gate.

The actual runtime/mailbox suite covers 17 scenarios with simulated USB/radio/
workers/NVS faults/ACK encryption notification. It checks old parameter-buffer
stability, exact ACK-only matching, queued/late barriers, both settlement paths,
CAS/consent/CRC/MAC rejection, blocked unknown/cleanup/reload failures and unchanged
nonowner NVS/USB enumeration. Actual BLE adapter tests separately exercise Info
and encrypt publication with fake SDK crypto, including a thread-held publication
lease, valid denial without terminal latch and malformed-frame terminal denial.
The existing cloud runtime and default-off combined runtime are rerun as
regressions. Evidence and failed attempts stay in `artifacts/owner-login-*/`.

Final native source hashes were reverified after all five suites completed:

| Suite | Passed build/run records | Subcase scope |
| --- | ---: | --- |
| model/wire | 4 | enabled/default-off policy, chunks, deadlines and handoff |
| store | 5 | 15 storage groups plus disabled link/symbol checks |
| actual Security2 | 11 | actual Store/SDK/reference authentication and AEAD |
| actual owner runtime | 18 | build plus 17 synthetic runtime scenarios |
| actual BLE adapter | 20 | new barrier tests plus 7 old timing/6 callback cases |

The existing feature-disabled combined runtime passed 45 records, and the
existing owner-cloud runtime passed its build plus 15 scenarios. Build records
and scenario groups are reported separately rather than added into a misleading
single unit-test count.

All native acceptance requires empty stderr, fatal ASan/UBSan, leak checks and
source-stable input hashes. Earlier harness errors remain recorded: a synthetic
Proof enum typo, a missing adapter esp_err fixture, and a no-ACK deadline test
that originally ignored the production loop's 5ms delay. They were test fixes,
not reasons to change production safety deadlines or suppress diagnostics.

Release remains blocked pending a coordinated enabled/default-disabled target
build and image/hash/map review, integration with the phone source, fresh private
recovery material, actual Android/GATT write/ACK/reconnect behavior, physical
NVS/power-loss and heap/stack/USB tests, and same-configuration backup resumption.
The separate real-SDK authentication test plus mocked runtime tests do not prove
an end-to-end physical phone rotation. No install or public deployment is implied.
