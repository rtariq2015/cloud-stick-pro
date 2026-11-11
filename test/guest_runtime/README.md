# Guest diagnostic owner-loop checks

Run `python3 test/guest_runtime/run.py` from the project root. The runner needs
Clang with C++17, AddressSanitizer and UndefinedBehaviorSanitizer, plus the pinned
ArduinoJson dependency already used by the project. `CSP_TEST_CLANGXX` can select
the retained compiler. It creates only temporary synthetic fixtures and binaries.

Eight scenarios compile and execute the **actual** `src/wifi_setup_runtime.cpp`,
Wi-Fi model, wire protocol and NVS adapter. They reuse the existing combined
runtime fixture; the small `CSP_GUEST_RUNTIME_TEST` guard keeps its old test main
out of this binary. The ordinary runtime tests are unchanged when that test-only
define is absent.

The cases check that:

- Startup and thirty simulated minutes never start the backup worker.
- Missing ownership, active BLE and failed listener cleanup refuse a test start.
- BOOT and the local bench-opening command cannot open BLE while diagnostics run.
- Saved-network recovery and dashboard admission wait while the diagnostic owns
  the network; USB polling continues throughout those waits.
- Cancelling permits normal saved-network recovery and dashboard admission.
- The authenticated status and Wi-Fi transaction entry points remain available
  after cancellation.
- The manual connection-check and power-save commands are rejected while the
  worker is stopped, with no Wi-Fi power driver calls.
- The synthetic saved Wi-Fi, owner and cloud records remain unchanged.

USB, radio, BLE, dashboard, diagnostic task, worker, clock and NVS I/O are
simulated. The diagnostic orchestration and internet transport have separate
actual-source fixtures in the adjacent directories. This harness does not prove
physical USB timing, radio reception, real certificate validation or a connection
to Guest-Net.
