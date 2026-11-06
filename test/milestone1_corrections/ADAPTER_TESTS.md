The adapter corrections are exercised directly by including the production
`src/ble_management.cpp`, rather than reimplementing its policy in a helper test.
Historical review files remain unchanged. The clock interleavings and actual
SDK SRP/GCM harness derive from that preserved evidence, with corrected expected
outcomes and additional generation/terminal response-boundary cases.

Run inside the development container via the repository compose wrapper:

```sh
python3 test/milestone1_corrections/build_adapter_tests.py
python3 test/milestone1_corrections/run_adapter_tests.py
```

The build verifies pinned source hashes and the reviewed minimal SDK patch
through `tools/pinned_ble_sdk.py`. Actual-SDK cases authenticate using the public
synthetic fixture, inject bad GCM tags, invalid-length frames and invalid
application grammar, and require immediate denial of valid requests, handshake,
encryption and reconnect before any poll. Closing a session cannot clear denial;
only a new owner-opened physical window can admit a fresh authenticated session.
Normal close/reconnect and duplicate application-ID failure are also covered.

All subprocesses use strict ASan/UBSan, leak checks and empty-stderr enforcement.
The seven timing cases preserve the original callback clock hooks and add exact
timeout/wrap, stale session/activity generation and absolute-window coverage.
Six SDK-return/clock-hook cases verify terminal and stale response disposal.
Clock, heap, MAC, transport lifecycle and radio are stubs: these tests establish
adapter policy and real SDK cryptographic interoperability, not RF, target
scheduling, real controller teardown, physical memory or USB acceptance.

The isolated boot/runtime regression is separate:

```sh
python3 test/milestone1_corrections/run_isolation_tests.py
```

It includes actual `main.cpp` and `ble_isolated_runtime.cpp`, actual lifecycle
policy/diagnostic grammar and pinned ArduinoJson7.4.3. Two normal/failure cases
check absent production side effects, USB initialization/polling, physical-only
admission, no reset after a ten-second hold, mutation-command refusal and the
seven real runtime partition/raw flash-denial wrappers. Hardware interfaces remain stubs.
Results and source hashes are under `artifacts/milestone1-corrections/isolation`.
