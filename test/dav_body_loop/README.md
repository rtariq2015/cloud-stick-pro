# HTTP response cancellation and idle budget

Source-only regression against the frozen Arduino 3.3.12 HTTP implementation
used with the pinned 55.03.312 platform. No SDK file is modified. No physical
device, cloud account, socket, private file, or microSD is accessed.

The exact pinned `HTTPClient::writeToStreamDataBlock` loop has no inactivity
deadline. `NetworkClientSecure::read` can return -1 while TCP remains connected;
inherited `NetworkClient::readBytes` then returns zero. HTTPClient passes zero
bytes to the sink, accepts a zero return, and repeats. For an unknown-length
body, an available count of zero skips the sink altogether. Returning zero from
a cancelled sink alone cannot terminate either case reliably.

The project-local `pro::http_response::Guard` and derived `TLS` in
`include/http_response_guard.h` keep the SDK's HTTP framing and chunk parser.
They arm only the inactivity clock after response headers. The guard
terminates a body after 15 seconds without positive plaintext progress, and
retains at most the existing 30-minute hash/readback absolute limit or 30-second
public probe absolute limit. The explicit total start is not reset by arming;
the regular readback budget now includes its preceding request setup. Clock
deltas use unsigned subtraction. Regular operations
observe backup maintenance and, for guarded readbacks, source validity; the
public probe observes its independent cancellation while maintenance remains
enabled. All DAV/source checks are supplied through an explicit callback; the
shared header has no DAV, health-publisher or storage dependency.
Cancellation/source invalidation maps to `SnapshotChanged`; a body
deadline maps to -2012. These terminal facts survive connection teardown and
prevent a matching partial count from being accepted as verified.

The facade polls that guard through virtual `connected`, `available` and `read`
methods, so idle unknown-length bodies can stop without a sink call. It closes
the same TLS connection on terminal rejection. TLS initialization, CA/hostname
verification, redirect policy, handshake/socket timeout and request framing are
unchanged. No other thread deletes the client and no asynchronous timer touches
the TLS object. Guard lifetime precedes TLS/HTTP lifetime. Zero/negative reads
of a nonzero requested size yield one RTOS tick, so a pinned Stream poll cannot
starve the idle watchdog; successful reads and zero-size connected probes add
no delay. A blocking SDK operation may still finish its existing finite timeout;
this change does not claim immediate mid-call cancellation.

Hash, destination-property and public read-only sinks set persistent Print write
error on rejection, including zero-byte input. Their failed state remains
terminal when the pinned SDK clears the Print error before its one short-write
retry. Readback/property/probe callers preserve the guard's cancellation/timeout
result after the HTTP scope closes. Upload framing and existing safeguards are
unchanged.

`run.py` verifies SHA-256 of each original SDK file, extracts the exact functions
without editing their bodies, and compiles them with the actual WebDAV adapter.
The HTTP body/framing functions, NetworkClient readBytes, Stream timedRead and
Stream readStringUntil execute against a controlled byte source. Stream methods
are bound to the synthetic NetworkClient via a class-name macro; their bodies
remain verbatim. One `-Wsign-compare` compiler diagnostic is locally disabled
only around the pinned NetworkClient function's existing signed deadline
comparison. ASan/UBSan remain nonrecovering everywhere; no sanitizer diagnostic
is suppressed.

The 20 focused groups cover connected idle known/unknown lengths, negative/zero
reads, falsely positive availability, cancellation, rollover, positive partial
data then stall, a stalled chunk line, a valid chunked readback, property limits,
terminal zero-byte sink errors, explicit source cancellation, total-deadline
preservation, sticky generic cancellation, independent probe cancellation, and the actual
Backup state machine/receipt codec. The last test confirms a stalled readback
keeps the saved record Transferred with a retry error and never writes Verified.
Native TCP/TLS, radio, timing, source and receipt-storage boundaries are simulated;
this is not physical networking or watchdog acceptance.

Run `python3 test/dav_body_loop/run.py`. Results, extracted function hashes and
strict sanitizer logs are in `artifacts/dav-body-loop/results.json`. Related
regressions are `test/backup_integration/dav_cancel.test.py`,
`tools/test_readonly_cloud_probe.py`, and `test/upload_diagnostics/run.py`.
Existing native TLS/Stream stubs only gained the matching virtual read/Print
error APIs; they remain synthetic and do not bypass any production checks.
