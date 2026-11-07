# Firmware and protocol checks

Tests here use native models, synthetic transports and mock services unless a
specific procedure explicitly says it requires a physical stick. Read the test's
own README before choosing a hardware procedure.

| Area | Entry point |
| --- | --- |
| Core backup, receipts and WebDAV failures | `bash test/run.sh` |
| Bluetooth policy models | `bash test/run_ble_native.sh` |
| Product-profile admission | `python3 test/product_rc/profile.test.py` |
| Reuse of an unchanged scan | `python3 test/scan_idle_gate/run.py` |
| Display model | `python3 test/lcd_v2/run.py` |
| Local dashboard | `python3 test/local_dashboard/run.py` |
| Saved-network retries | `python3 tools/test_wifi_reconnect.py` |
| NVS settings-cache cleanup | `python3 tools/test_nvs_cache_release.py` |

The [desktop tests](../desktop/tests/run_native_tests.py) cover native boundaries, setup state,
firmware signatures, encrypted recovery and injected installation failures.
[Android tests](../android/README.md#build) cover its nearby viewer. Native package CI
also tests the Windows installation and removal using synthetic data.

A passing model or mock-server test is not a physical USB, Wi-Fi, cloud-account
or new-board installation test. Exact acceptance belongs in
[release records](../docs/RELEASES.md). No ordinary test command here grants
permission to erase, flash or format a stick.
