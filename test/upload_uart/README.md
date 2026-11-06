# Read-only upload UART diagnostics gate

This native test links the actual `wifi_setup_runtime.cpp` owner loop and UART
dispatcher, with synthetic USB/NVS/radio/worker fixtures and a controlled
`upload_diagnostics_snapshot()` accessor. It does not exercise an actual UART,
TLS backend, cloud service, filesystem or physical device.

Run through the workspace container wrapper:

```bash
./compose.sh -f docker-compose.yml \
  -f build/research/cloud-stick-auth-cleanup-compose.yml \
  exec -T --user 1000:1000 \
  --workdir /home/home/tool.plugin-builder/build/research/cloud-stick-public-wifi-stage \
  unmanic-dev python3 test/upload_uart/run.py
```

The runner builds default, explicitly disabled and explicitly enabled variants
with fatal ASan/UBSan and leak detection. Successful commands must have empty
stderr and no sanitizer diagnostic. Its result is
`artifacts/upload-uart/results.json`, including hashes of the tested production
runtime, diagnostic header and test source, checked unchanged through the run.

The enabled variant checks all 38 exact allowlisted fields and their integer or
boolean types, including uint64 values above JavaScript's exact-number range,
UINT64_MAX, UINT32_MAX, INT32_MIN, negative TLS return codes and independently
captured write/read errno. It checks an empty snapshot and a later HTTP 507
observation without translating any counter into verified upload success.

The default and disabled variants preserve the existing generic
`{"accepted":false}` response to an unknown `UPLOAD_STATUS` command. They do
not expose diagnostics. Arguments, suffixes and lowercase commands receive the
same rejection; control characters use the preexisting overflow/drop path.

After all requests, assertions compare NVS contents/write counts, worker
phase/generation/start/retire/reap counters, loaded cloud configuration, radio
connection/check counts, maintenance state and USB initialization. Synthetic
private filenames, account markers and configuration bytes must not appear in
the diagnostic response. No production or shared fixture source is changed by
this test.

Physical UART response framing, real producer synchronization/errno capture,
USB responsiveness and actual transfer behavior remain separate acceptance
tests. The returned numeric observations alone are not server acceptance,
receipt verification or proof of a completed backup.
