# Public Wi-Fi preparation clock correction

2026-11-04. **Source/desktop gate: PASS. Physical correction test: pending.**
No device, cloud account, phone/site source, SDK, installer/helper or frozen lab1
firmware artifact was changed by this lane. The integration owner reported a
lab1 authenticated portal attempt returning Complete/Expired 5 before routing
started; the source tests reproduce that same state transition.

## Proven defect

The owner loop sampled `now` before processing its mailbox. `portal_begin`
then entered Prepared using a newer `millis()`. Calling `publicTick(now)` with
the older sample made unsigned `now - at` appear close to 2^32 milliseconds.
Both the timeout test and the remaining-time snapshot treated this as a very
old session: the coordinator immediately entered Complete with Expired 5.

For example, `at = 1000`, `now = 999` gave a computed age of 4294967295 instead
of zero. This is unrelated to whether Security 2 authentication succeeded.

Two narrow source changes correct it:

- Sample `millis()` for `publicTick` **after** mailbox `worker()` dispatch.
- Share a bounded age calculation in Coordinator timeout and snapshot paths.
  A delta at or above half the uint32 clock range is stale/ambiguous and has
  age zero. Forward uint32 rollover still produces the correct small delta.

All phase deadlines are far below half the clock range. As with ordinary
uint32 timer comparisons, this assumes phase polling resumes within half a
millis cycle; a 24-day stalled task is not validated by these tests. No deadline,
physical/session authorization, rejection barrier, memory gate, watchdog, USB
ownership or TLS protection was widened or disabled.

## Reproduction and correction evidence

`clock_regression.py` extracts the **exact lab1 source** from commit
`212090dcf7eef454674ec24e50f9d0abf7059ed5` into a separate test artifact
directory. It compiles actual Coordinator/owner-runtime code with strict
ASan/UBSan and leak detection. Only RTOS/radio/USB/NVS are synthetic.

| Combination | Result |
| --- | --- |
| Lab1 policy, stale `at - 1` snapshot | Expected assertion failure: zero remaining time |
| Lab1 runtime + lab1 policy, advancing `millis()` during dispatch | Expected assertion failure with **`phase=complete error=5`** after begin |
| Corrected runtime + lab1 policy | PASS: fresh owner sample alone prevents the reproduced runtime failure |
| Lab1 runtime + corrected policy | PASS: defensive age guard alone protects the stale owner sample |
| Corrected policy clock suite | PASS, 6 groups: stale samples, real exact deadlines, expiry, rollover and later phases |
| Corrected runtime + policy, advancing clock | PASS, 10 runtime scenarios including normal handoff, cancel, genuine expiry, terminal rejection, route/probe failures, timeout and low memory |

The clock wrapper leaves common fixtures unchanged. It renames their fixed
`millis()` while including existing stubs, then provides an incrementing
`millis()`. Advancing time is enabled after the fixture's BLE connection is
established so the regression targets mailbox/public-coordinator ordering.
An earlier attempt that advanced setup-button fixture time also exposed an
unrelated fixture admission assertion; that attempt is not claimed as the
portal reproduction.

Evidence: `artifacts/public-wifi-clock/results.json`, **21 command records**,
SHA-256 `56040d693403eb223b2b16a044a48742d685016dd61643c49aa72190df237786`.
Expected old assertion failures are explicitly marked as negative tests; no
sanitizer diagnostic is accepted as a passing reproduction. Successful builds
and tests have empty stderr. Source hashes stayed unchanged during the run.

The expanded normal gate `test/public_wifi_assistance/run_tests.py` also passed
all **34 command records**, including the existing policy, default/disabled/
enabled wire, normal runtime and probe-task checks, plus the new clock policy
and ten advancing-clock runtime scenarios. Its results SHA-256 is
`6dfce220a5d649b7e26be6912dbd81c54aaa407b4d17b5eab0725ba4ba6e235a`.

## Reproduce

Run each command through the source container from the workspace root:

```bash
./compose.sh -f docker-compose.yml \
  -f build/research/cloud-stick-auth-cleanup-compose.yml \
  exec -T --user 1000:1000 \
  --workdir /home/home/tool.plugin-builder/build/research/cloud-stick-public-wifi-stage \
  unmanic-dev python3 test/public_wifi_assistance/clock_regression.py
```

Replace the final script with `test/public_wifi_assistance/run_tests.py` for
the full public-Wi-Fi source gate. These commands compile native test programs;
they do not build target firmware or contact the dongle.

The corrected candidate still needs the integration owner's image-specific
build/recovery review and a controlled physical test. Passing this source gate
does not establish that a venue portal, temporary AP, cloud probe or backup
resumption has worked on hardware.
