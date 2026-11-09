# Upload speed work

The next firmware candidate, **0.4.9-upload-dev**, avoids asking the cloud to
create the same folders again for each file in a batch. This is intended to
reduce waiting between small files. It is a development candidate, **not an
installed or offered firmware update**. The installed firmware remains 0.4.8;
the failed real upload baseline below remains unresolved.

The stick remembers just the last parent folder and whether it prepared its
temporary upload folder. This memory lasts only for the current backup worker.
Any failed upload or verification discards it. If a destination folder disappears,
the normal verification retry prepares it again before trying to publish the file.

Every file still needs a successful upload, a complete hash readback, the existing
destination protection, publication and another complete readback before a
verified receipt is saved. TLS checks, timeouts, USB ownership, settings and the
receipt format are unchanged. The change adds a fixed 241-byte parent buffer and
one flag before alignment; it does not retain additional file contents or cloud
credentials. Earlier firmware profiles leave this feature disabled.

## What the computer tests show

The [actual DAV adapter test](../test/dav_collection_cache/adapter.test.cpp) runs
the production transfer and receipt code with simulated HTTP, TLS, storage and
time. For ten distinct files in the same folder three levels deep:

| Request type | Existing behavior | Candidate |
| --- | ---: | ---: |
| Folder preparation | 40 | 4 |
| File upload | 10 | 10 |
| Complete content readbacks | 20 | 20 |
| Destination type checks | 10 | 10 |
| File publication | 10 | 10 |
| Total | 90 | 54 |

That is 40% fewer requests in this particular simulated batch, **not a measured
40% increase in upload speed**. The benefit depends on folder layout and the
cloud connection. It does not increase the raw Wi-Fi rate or remove the necessary
transfer and verification of large files.

Both disabled and enabled variants pass 23 case groups under AddressSanitizer
and UndefinedBehaviorSanitizer. Cases include authentication/server failures,
lost folders, mismatched contents, exact parent matching, cancellation, maximum
path length, worker replacement and conditional overwrites. The lost-parent
candidate case exercises the real receipt retry without uploading the file twice.

Run the source-only test with `python3 test/dav_collection_cache/run.py` on a
Linux machine with a C++17 compiler and OpenSSL development headers. Its generated
report records the exact source hashes and explicitly marks physical-device and
real-cloud testing as false. GitHub runs the same check for proposed changes.

## What still needs measurement

The earlier cache-only build passed at source
`d1232ba595ba6e40410c2ceb13d9bd5dad1ad8d8`. Its application is 1,701,648 bytes,
SHA-256 `3c067ded61a7bdb91ba2e2b724408636bd9ce7ddde5567d966f7d1619421cbf2`.
All 6,509 frozen SDK entries matched before and after, using manifest
`1667e4e5650d161240a3ba6a271f02004717ddc1caec56cf178805f8e0738237`.
The partition image and USB/storage source gates passed. Target debug type
information records a 252-byte DAV client versus 8 bytes before this change,
a 244-byte increase in the worker object, with no persistent-data change.
The 61 guarded-build self-tests, existing cancellation/read-only probe/body-loop/
pinned stream regressions and 36 publication-policy cases passed. All six
relevant PR checks passed at [Actions run 42561110115](https://github.com/rtariq2015/cloud-stick-pro/actions/runs/42561110115).

It still needs installation through the protected installer, then a real
small-file batch and a large-file comparison on
the same Wi-Fi and cloud account. Record bytes, upload duration and the separate
verification duration; report failures as well as successful runs. Preserve
existing files and remove only unchanged synthetic test files created for the
measurement. No real-cloud speed increase has been verified yet.

The current app's live rate is a rolling sample. Dividing total bytes by that
sample would not give an accurate upload duration. Accurate completed-upload
timing remains separate work and must use the device's measured start/end times.

## Real baseline attempt on the existing 0.4.8 image

A later real baseline created five uniquely named synthetic files totalling
1,180,333 bytes in the watched folder, without changing existing files. It did
not produce a valid speed result: the public dashboard stopped responding during
upload, and independent USB later reported `-4` (the pinned HTTP client's
"not connected" error), with the stick still associated to Wi-Fi, BLE closed,
backups unpaused and no store fault. The trial's speed measurement is failed,
not a successful throughput result or evidence that this candidate solves it.

Existing verified files remained counted. The five test files were left for a
bounded observation of normal retries; exact test-file receipts and cleanup are
separate from the failed measurement. Raw device metadata and filenames remain
private. No upload-candidate firmware was installed. The exact failing cloud
request and root cause still need investigation before claiming faster or more
reliable uploads.

## Next diagnostic candidate: numeric cloud-request observations

The real baseline's five synthetic records were later read through ordinary
personal authentication in the installed Setup 0.7.3 app. All five are
**Transferred**, and none are **Verified**: the server acknowledged their PUTs,
but the later verification/publication chain did not finish. Done and independent
USB checks restored memory/Wi-Fi/backups, with no reset or saved-network revision
change. This narrows the investigation without identifying the failing request.

The next 0.4.9 candidate also adds a bounded RAM-only `CLOUD_TRACE` USB observation.
It distinguishes HTTP header results, response-body return values and the original
operation result for staging PUT/readback, destination type check, publishing,
final readback, replacements, folder preparation/checks and optional cleanup.
It keeps the current request, last completed request and last negative request;
all values are enums, counters, elapsed milliseconds or numeric codes. There is
no URL, filename, header content, account, password, data buffer or allocator pointer.

The trace is off in earlier profiles. It does not change requests, results, retries,
TLS validation, deadlines, receipt decisions, storage ownership or SDK code.
Snapshots use a short lock; formatting/USB output happens after the lock is released.
An unexpected nested scope cannot replace the admitted request. Durations include
that operation's connection/body work, not total upload-to-verified time. `body`
is `-2147483648` when no response body was read. A per-request error is not itself
a file-backup outcome, especially for optional cleanup or staging 404 fallback.

The USB enum mapping is: 1 parent folder, 2 staging folder, 3 staging PUT,
4 staging readback, 5 destination type check, 6 publication, 7 final readback,
8 replacement PUT, 9 cleanup, 10 folder-access check, 11 root-folder preparation.
The operator should correlate these observations with the ordinary backup result.
Private recovery and an explicit guarded installation are still required before
this diagnostic can be used on the real stick. It has not been installed.

The complete cache-and-trace build passed at exact source
`ed1a247b600c842ade70bcc61da3feea76734f44`: application 1,703,776 bytes,
SHA-256 `49af6057b3942e551c4bc9a6a12e55a578da68949db9ec3be218013b2a6e61e3`.
All 6,509 frozen SDK entries matched before and after with the manifest above;
the existing partition and USB/storage safety gates passed. This supersedes the
earlier cache-only candidate for the next diagnostic test. A signed private
six-image package passed the production catalog and bundle validators without
contacting hardware or changing the normal app cache. It is not a public update.

The 23 actual-adapter case groups pass with tracing/cache disabled and enabled.
Additional cases distinguish a staging versus final readback body error, a final
GET header error, a failed MOVE and the existing property-body error mapping.
Trace tests cover clock wrap, nested scope exclusion, retained last-negative
observations and coherent snapshots across threads under ASan/UBSan. Existing
cancellation, pinned upload/body-loop and Wi-Fi/runtime regressions, 61 guarded
build self-tests and 36 publication-policy cases also pass. All six relevant
checks passed at [Actions run 42567472267](https://github.com/rtariq2015/cloud-stick-pro/actions/runs/42567472267).
These are computer tests with simulated transport/storage, not acceptance on the
stick or proof of a real-cloud speed improvement. [Issue #24](https://github.com/rtariq2015/cloud-stick-pro/issues/24)
tracks the actual upload failure; release promotion remains on hold.
