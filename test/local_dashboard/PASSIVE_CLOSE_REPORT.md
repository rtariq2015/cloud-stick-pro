# Bounded passive-close source experiment

Prepared 2026-11-05 in isolated `feature/dashboard-bounded-passive-close`, from
`d30ed5a`. No device, serial, saved configuration, cloud account, website, SD,
receipt, PlatformIO or linked firmware-image operation was performed. Root owns
integration and any separately controlled target test. The changes remain
uncommitted for root review.

## Implemented behavior

`CSP_DASHBOARD_PASSIVE_CLOSE` defaults to **0** and accepts only0/1. Flag0 keeps
the existing immediate close after all header/body bytes were accepted by BSD
`send`. No production profile was changed.

With flag1, successful completion of the full known Content-Length response:

1. Immediately deletes the single caller-owned response/request buffer and clears
   every body reference/counter. BSD send copies the supplied bytes; TCP never
   borrows those application arrays after the call returns.
2. Retains the existing descriptor and a small owner-loop wait state. Each later
   poll makes at most one nonblocking `recv(...,1,MSG_PEEK)` call.
3. Peer FIN/EOF causes normal close. Unexpected bytes or a terminal receive error
   also cause normal close. EAGAIN/EWOULDBLOCK/EINTR return to the owner loop;
   they do not spin or renew any deadline.
4. A client waiting for server EOF gets ordinary close at250 ms after completion.
   The original accepted+3000 ms total lifetime always wins if sooner. Both use
   unsigned wrap-safe elapsed-time comparisons.

Explicit stop, disallowed setup/public-routing state, memory pressure, absent
address and address changes still close immediately. No SO_LINGER/reset behavior,
global TCP timeout, SDK setting, watchdog, extra thread/task, timer or heap
allocation was added. Ambiguous close failures retain the terminal fault and
never retry the numeric descriptor, including after FD reuse. Buffers already
released at successful response completion remain safely released.

HTTP headers, Content-Length generation, CSP, accepted routes, JSON schema,
dashboard assets, USB behavior and backup receipts are unchanged.

## What motivated the experiment, and what remains unknown

The parent reported a strict observer's short response before a later actual
HTTP/TLS run completed149 responses plus22 unavailable responses. Its exact
short-response cause is still unknown. This source experiment neither reproduces
that target failure nor proves that immediate close caused it.

Espressif documents that closed/recreated TCP connections can coexist while the
TCP state machine finishes, consuming on-demand stack memory. This supports
investigating close order; it is not a measurement of this product's PCB cost.
See [ESP-IDF5.5.5 lwIP memory/state guidance](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/lwip.html#minimum-ram-usage).

The heap minimum API aggregates per-region low watermarks that may occur at
different times. It must not be treated as the instantaneous free heap observed
in a particular HTTP/TLS overlap. Largest-block and current-free observations
also describe different quantities. See [the pinned heap API caveat](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/mem_alloc.html#_CPPv431heap_caps_get_minimum_free_size8uint32_t).

No ESP memory savings, target latency improvement, transfer speed improvement or
fix for the short-response observation is claimed.

## Exact allocation and layout changes

| Quantity | Flag0/default | Flag1 | Evidence/limit |
| --- | ---: | ---: | --- |
| Caller buffer requested per accepted client |3201 bytes|3201 bytes| Actual native nothrow allocation/deletion accounting; unchanged char-array layout |
| Caller buffer remaining after complete response |0|0| Both variants release it at completion; flagged path does so before waiting for FIN |
| New application heap allocation |0|0| No new allocation in source or passive wait |
| Native64-bit `sizeof(Server)` |112|120| +8 bytes |
| ESP32-S3 `sizeof(Server)` |80|88| +8 bytes from pinned target compiler header-only object symbols |
| Default flag |0| — | Default and explicit0 symbol layouts match; invalid2 fails compilation |

The ESP32-S3 result is a header-only object layout check, **not a firmware build**.
Compiler identities/hashes and object hashes are recorded in
`artifacts/local-dashboard-layout/results.json`. The application singleton would
therefore add8 static bytes when enabled. Allocator metadata, TCP receive/send
buffers/PCBs, complete firmware map and target stack high-water marks remain
unmeasured. The wait path introduces one stack byte plus ordinary call/local
state; no exact target stack-frame saving or increase is claimed.

## Executed tests

Both flag0 and flag1 passed the existing native parser/model fuzz, real fragmented
HTTP server, strict lengths/headers, denied mutation/host/body,3-second timeout,
memory/ownership shutdown, allocation failure,100 starts/stops and terminal
ambiguous-close/reused-FD tests. Each parser/model run includes100000 hostile
headers. Existing dashboard JavaScript tests also passed.

Directed native tests additionally cover:

- Full Content-Length-first clients, which close only after every header/body
  byte is counted, with exact expected body bytes.
- Independent complete JSON parsing, exact field set/types/values, and no
  acceptance of a substring or incomplete JSON object.
- Peer-first FIN; EOF-waiting fallback at249/250 ms; total lifetime winning near
  completion; uint32 clock wrap; one peek per poll; EAGAIN/EINTR and terminal EIO;
  late unexpected input; stop/memory/routing/address changes between finish/FIN.
- Immediate3201-byte buffer deletion before the passive wait, balanced allocation
  and deletion counts, no double free/use-after-free, and terminal close behavior
  after the underlying FD is recycled.

The actual owner-loop lifecycle suite passed normal-close, uncertain-close,
snapshot-only and config-invalid for **both flag variants**. Radio, USB, NVS,
worker, memory and dashboard socket boundaries in that suite are explicitly
synthetic. Its output is not a physical USB/BLE acceptance result.

All native executable suites use strict ASan/UBSan/leak checks with no sanitizer
diagnostic or unexpected stderr accepted. The source runner now records complete
and stable-source gates explicitly; partial successful builds cannot make its
overall result PASS.

## Native TCP close-order observation

In24 loopback Content-Length-first requests per variant, Linux `/proc/net/tcp`
showed:

| Build | TIME_WAIT with local server port | TIME_WAIT with remote server port |
| --- | ---: | ---: |
| Flag0 |24|0|
| Flag1 |0|24|

This shows close-order behavior for this native Linux fixture and does not measure
ESP/lwIP RAM, bound lwIP PCB lifetimes or establish target behavior. Tests record
the available counts without assuming every platform must report those values.
EOF-waiting clients still trigger the bounded server-first fallback.

## Evidence and rerun

Evidence directories:

- `artifacts/local-dashboard-source/`: both variant builds/runs, strict JSON,
  allocation/FIN/TIME_WAIT facts, existing server/parser fuzz and JS logs.
- `artifacts/local-dashboard-runtime/`: both owner-loop variant builds and four
  scenarios per variant, source hashes and explicit synthetic boundaries.
- `artifacts/local-dashboard-layout/`: native/ESP32-S3 header layouts, default0
  proof and invalid-flag compiler rejection.

From `/home/home/tool.plugin-builder`, use this existing compose prefix with each
command below:

```sh
./compose.sh -f docker-compose.yml -f build/research/cloud-stick-auth-cleanup-compose.yml exec -T --user 1000:1000 --workdir /home/home/tool.plugin-builder/build/research/cloud-stick-http-close-stage unmanic-dev
```

```sh
/config/.unmanic/research/cloud-stick-tools/bin/python3 test/local_dashboard/run.py
env CSP_DASHBOARD_TEST_DEPENDENCY_ROOT=/config/.unmanic/research/cloud-stick-product-release-stage /config/.unmanic/research/cloud-stick-tools/bin/python3 test/local_dashboard/runtime.test.py
/config/.unmanic/research/cloud-stick-tools/bin/python3 test/local_dashboard/layout.test.py
```

The explicit dependency-root override reuses already-installed ArduinoJson7.4.3,
verified by the existing pinned dependency locator. It performs no download or
PlatformIO operation. The first isolated runtime invocation correctly failed
because this new worktree had no local `.pio/libdeps`; the successful rerun used
that explicit existing dependency root.

Before target enablement, root must build the integrated image, inspect the exact
map/hash and run controlled complete-response/HTTP+TLS/heap/USB/BLE transition
tests. A failed strict body/JSON check remains failure; neither grace timeout nor
an unavailable response should be used to conceal a truncated response.
