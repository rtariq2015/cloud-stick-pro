# Guest internet probe software checks

Run `python3 test/guest_internet_probe/run.py` from the project root.

The runner compiles the actual `src/guest_internet_probe.cpp` with AddressSanitizer
and UndefinedBehaviorSanitizer. The small SDK, Wi-Fi, DNS, socket and FreeRTOS
adapters are simulated; no socket, phone, dongle or cloud account is contacted.

The cases exercise exact credential-free GET requests, no request bodies or
redirect following, stopping before response bodies, trust-bundle and hostname
configuration, DNS/clock/HTTP/TLS errors, partial writes and read errors. The
actual response reader is checked at its 4096-byte limit and its five-second
whole-header deadline, including slow byte streams, malformed status lines,
closed/idle sockets and timer rollover. Allocation failures, owner/core checks,
cancellation and suspended-task retirement are also exercised.

These checks do not validate a real certificate chain, the SDK's DNS/connect/
write implementations, radio coexistence, an actual portal, hardware memory
margins or USB responsiveness. Those need target build review and physical
acceptance. The simulated scheduler throws an internal test exception at task
suspension; it does not create or delete a real RTOS task.
