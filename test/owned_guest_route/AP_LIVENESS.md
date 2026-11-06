# AP liveness during public sign-in assistance

Source/native correction based on `8ebe8d758a8725ceae81482da59f57bcb238ac3c`.
This change has not been built for or installed on a device by this test lane.
It is separate from the earlier host controller's strict network-proof timeout;
that physical failure was not shown to have been caused by an AP stop.

The actual `owned_guest_route.cpp` now proves AP liveness before admitting
sockets/NAPT and on each Active poll: current mode is APSTA, the retained AP
netif reports up, AP_START was observed, and no later AP_STOP was observed.
The netif check uses pinned ESP-IDF's `esp_netif_is_netif_up(esp_netif_t*)`,
declared in the frozen SDK's `esp_netif.h:388`. It adds no task, allocation,
timer, network request or new timeout.

A failed proof leaves Active with `ApProof` and enters the existing owned stop
path. NAPT disable, descriptor closure, AP stop, event barrier/unregistration
and memory release retain their independent proofs. An uncertain cleanup remains
Blocked with ownership retained. No station disconnect, USB operation, metadata
operation, security change or reset was added.

The existing pre-start event barrier drains old callbacks before clearing AP
flags and configuring the new generation. Such older callbacks cannot poison
its startup. After that barrier, AP_STOP is terminal for the generation even
if an unexpected AP_START follows: Wi-Fi's generic event does not carry a route
generation that could justify ignoring a later stop. Expected teardown events
remain proof on the Stopping path; they do not introduce a new fault.

`ap_liveness_tests.py` links the actual old/current adapter against synthetic
SDK/netif/event/socket fixtures. The old source fails both AP-only stop and
netif-only loss scenarios; the corrected source passes seven groups covering
those failures, stale pre-start callbacks, unrelated events, late stop/start,
startup admission and expected/post-teardown callbacks. The fixture now gives
WIFI_EVENT one shared identity across native translation units, matching real
ESP event-base identity; it does not infer link state from a queued event.

Results are in `artifacts/owned-guest-liveness/results.json`. Builds and tests
use nonrecovering ASan/UBSan with leak detection; sanitizer diagnostics cannot
pass. Existing owned-route fault/cleanup tests and public coordinator/runtime
tests are separate focused regression gates. They remain desktop simulations,
not real AP association, DHCP, RTOS timing or USB acceptance.

Reproduce inside the existing workspace compose container:

```text
python3 test/owned_guest_route/ap_liveness_tests.py
python3 test/owned_guest_route/run_tests.py
python3 test/public_wifi_assistance/run_tests.py
```

The owner separately authorized the narrow runtime integration boundary: safe
resource cleanup is distinct from a successful assistance window. `publicTick`
now treats a Stopped route carrying an unexpected error as a failed route
observation, and `route_clean` accepts only `None` or expected window `Expired`
in addition to the unchanged resource and preserved-station proofs. Actual
runtime regressions prove cleaned `ApProof`/socket faults and retained resources
cannot admit a probe or restart backups, while normal/expired windows still
admit the verified-probe path. The old runtime fails the `ApProof` regression.
The earlier history-release block and all other owner behavior are unchanged.
