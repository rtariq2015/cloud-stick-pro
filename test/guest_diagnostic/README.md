# Guest Wi-Fi diagnostic orchestration tests

Run from the repository root:

```sh
python3 test/guest_diagnostic/run.py
```

The runner compiles the actual `src/guest_diagnostic.cpp` and its actual public
headers with a C++17 compiler, AddressSanitizer and UndefinedBehaviorSanitizer.
Each of 50 scenarios runs in a fresh process so production static state starts
normally. Fixtures provide a fake radio, DHCP lease, Wi-Fi state, serial output
and an explicitly drained internet task. No network or physical device is used.

Coverage includes rejected admission, scan errors and absence, all 16 protected
authentication codes, conflicting and truncated security observations, an
open-network request with an empty password, the two-attempt limit, radio faults,
lease read errors, task allocation failure, cancellation waiting for reaping,
DNS fallback/failure, HTTP and TLS result classification, clock failure, and
SSID/IP/state changes during and after probing. Cancelling an idle or completed
test must not disconnect an unrelated provisioning or completed connection.
Rejected or cancelled results must not show
an available internet connection. Starting another test clears previous results.

No storage, NVS, cloud worker or file implementation is linked. The fixture
rejects connection requests containing a password, a saved-profile ID or a
non-open authentication mode. This checks the diagnostic module's boundary;
whole-firmware upload exclusion and USB ownership require their separate runtime
checks. This suite does not execute the real DNS/HTTP/TLS transport, radio SDK,
LCD driver, or physical DHCP/Guest-Net connection.
