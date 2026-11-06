# Remote health publisher native gate

Run `python3 test/remote_health_publisher/run.py` through the source-container
wrapper. The gate compiles the actual publisher, WebDAV adapter, model, core and
tinyxml2. An in-memory DAV service supplies only synthetic resources. TLS/HTTP
objects assert the existing CA, timeout, no-reuse and no-redirect configuration;
they do not establish a real TLS connection or prove provider interoperability.

The 80 behavior cases cover first/alternating publication, exact namespace-marker
ownership, collections and unknown files, strong ETags and conditional races,
full-body corruption, partial headers, owned truncated-body repair, trailing
bytes, embedded-NUL headers, declared HTTP size mismatch, bounded unknown-length
responses, ambiguous conditional-create/replacement success statuses, HTTP
authentication/redirect/quota failures, retries after quota and
an unacknowledged commit, invalid observations/admission, every request-chain
retirement boundary, streamed upload/readback cancellation, and 30-second
admission deadlines including millisecond wrap. The runner requires fatal
ASan/UBSan behavior, no leak diagnostics, no unexpected stderr and no synthetic
credentials in output.

`storage_ready()` deliberately returns false. Every production-adapter `fopen`
is forbidden, as are backup transfer/progress callbacks. Existing unrelated
synthetic data must remain unchanged. Every HTTP request must stay under the
dedicated metadata namespace and carry the expected authentication, trust and
redirect policy. At most one mock TLS object and one HTTP object may be live.

Default and explicit-disabled publisher translation units are empty and expose
no publisher API on WebDAVClient. These checks concern source/link presence,
not an actual target firmware image.

`run_body_loop.py` adds 32 groups using the exact SHA-verified pinned Arduino
HTTP body/framing, NetworkClient `readBytes` and Stream line/read functions.
All three health GET body paths (owner marker, existing slot and final readback)
use the shared project-local `http_response_guard.h`. It polls cancellation and
deadlines through the same TLS client's virtual read/available/connected methods.
Unknown-length available-zero responses therefore terminate even without a sink
callback. Known-length negative/zero reads yield and terminate within the
existing 15-second inactivity budget. Arming a response body resets only that
inactivity clock; it preserves the original 30-second whole health attempt start.
The guard's terminal error survives HTTP teardown: retirement maps to
`SnapshotChanged`, while inactivity/whole-attempt expiry maps to `HealthDeadline`.
Neither can acknowledge or conditionally repair a stalled partial report.

Both health sinks retain their own terminal rejection state and set the Print
write-error flag even for zero-byte input. This remains terminal after the SDK
clears Print's flag for its one short-write retry. Exact pinned-loop tests cover
known/unknown idle cancellation, all no-data read modes, partial bodies,
millisecond rollover, delayed body arming, healthy identity/chunked framing and
stalled chunk lines. TCP/TLS bytes, radio and time remain simulated. One existing
signed comparison compiler warning is disabled only around the verbatim pinned
NetworkClient function; sanitizers remain fatal with no suppression.

Important limits:

- A failed initial namespace creation before its ownership marker becomes
  durable leaves an unmarked folder. Later attempts fail closed instead of
  claiming it. That deliberate safety result may require reviewed recovery;
  ordinary owned-namespace transient failures can retry successfully.
- A partial header or unknown schema is never overwritten. Only an exact owned
  complete header with a truncated body can be repaired conditionally.
- Report verification remains separate from file receipts and file-backup
  success. These tests exercise no actual SD card, receipt store or account.
- The native `.su` report includes the host ABI and mocked SDK types; it is not
  an ESP32 stack watermark. The enabled object sizes include in-memory fixture
  machinery and globals, so its data/BSS/text cannot be reported as production
  target cost. Default-off zero-object evidence remains valid.
- Actual Koofr conditional PUT/ETag behavior, physical TLS/network cancellation,
  target stack/heap margins and worker/USB concurrency remain untested. An
  already-blocking SDK operation retains its finite transport timeout; no new
  timer/thread deletes a TLS object or promises preemption inside that call.

No hardware, real network/cloud request, private vault, website or commit is
performed by this runner.
