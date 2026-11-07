# Firmware code

This is the software running on the original LILYGO T-Dongle S3. Start with the
area you need; build profiles remain in [platformio.ini](../platformio.ini).

| Area | Main files | Responsibility |
| --- | --- | --- |
| Startup and coordination | [main.cpp](main.cpp), [wifi_setup_runtime.cpp](wifi_setup_runtime.cpp) | Start services and coordinate setup, radio ownership and backup work |
| USB files and read sessions | [usb_storage.cpp](usb_storage.cpp), [sync_engine.cpp](sync_engine.cpp) | Keep the computer as the card writer and invalidate reads when it writes |
| Backup and verification | [pro_core.cpp](pro_core.cpp), [webdav_client.cpp](webdav_client.cpp), [pro_journal.cpp](pro_journal.cpp) | Upload, check cloud contents and save verified receipts |
| Login and settings | [owner_login_nvs.cpp](owner_login_nvs.cpp), [wifi_setup_nvs.cpp](wifi_setup_nvs.cpp), [owner_cloud_nvs.cpp](owner_cloud_nvs.cpp) | Preserve durable owner, Wi-Fi and cloud records |
| Bluetooth | [ble_management.cpp](ble_management.cpp), [ble_sdk_lifecycle.cpp](ble_sdk_lifecycle.cpp) | Authenticated management and checked radio cleanup |
| Screens and local status | [lcd.cpp](lcd.cpp), [local_dashboard.cpp](local_dashboard.cpp), [local_dashboard_assets.cpp](local_dashboard_assets.cpp) | Show current work separately from saved results |

Shared interfaces are in [include](../include/README.md); focused regression
fixtures are in [test](../test/README.md). Retained third-party integration inputs
and patches are in [sdk](../sdk/README.md).

Keep the storage-ownership, TLS, authentication, watchdog, receipt and partition
boundaries described in [storage safety](../docs/STORAGE_SAFETY.md). Compiling
or testing this source does not upload it to a stick. Actual device work has its
own reviewed procedure and serial-ownership checks.
