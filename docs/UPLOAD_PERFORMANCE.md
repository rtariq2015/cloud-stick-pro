# Upload speed work

The next firmware candidate, **0.4.9-upload-dev**, avoids asking the cloud to
create the same folders again for each file in a batch. This is intended to
reduce waiting between small files. It is a development candidate, **not an
installed or offered firmware update**. The offered firmware remains 0.4.8.

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

Both disabled and enabled variants pass 18 case groups under AddressSanitizer
and UndefinedBehaviorSanitizer. Cases include authentication/server failures,
lost folders, mismatched contents, exact parent matching, cancellation, maximum
path length, worker replacement and conditional overwrites. The lost-parent
candidate case exercises the real receipt retry without uploading the file twice.

Run the source-only test with `python3 test/dav_collection_cache/run.py` on a
Linux machine with a C++17 compiler and OpenSSL development headers. Its generated
report records the exact source hashes and explicitly marks physical-device and
real-cloud testing as false. GitHub runs the same check for proposed changes.

## What still needs measurement

The candidate needs a guarded firmware build and an installation through the
protected installer, then a real small-file batch and a large-file comparison on
the same Wi-Fi and cloud account. Record bytes, upload duration and the separate
verification duration; report failures as well as successful runs. Preserve
existing files and remove only unchanged synthetic test files created for the
measurement. No real-cloud speed increase has been verified yet.

The current app's live rate is a rolling sample. Dividing total bytes by that
sample would not give an accurate upload duration. Accurate completed-upload
timing remains separate work and must use the device's measured start/end times.
