# Firmware interfaces and small models

These headers connect the firmware's modules. Their implementations are usually
in [src](../src/README.md); small pure models can live entirely in a header.

| Boundary | Useful starting points |
| --- | --- |
| USB read-session validity | [pro_snapshot.h](pro_snapshot.h), [pro_blockio.h](pro_blockio.h) |
| Backup state and transfers | [pro_core.h](pro_core.h), [pro_device.h](pro_device.h), [pro_transfer.h](pro_transfer.h) |
| Worker ownership | [backup_worker.h](backup_worker.h), [backup_memory.h](backup_memory.h), [backup_wifi_power.h](backup_wifi_power.h) |
| Bluetooth state and memory | [ble_management.h](ble_management.h), [ble_sdk_lifecycle.h](ble_sdk_lifecycle.h), [ble_memory_status.h](ble_memory_status.h) |
| Recorded results and display | [file_history.h](file_history.h), [local_dashboard.h](local_dashboard.h), [lcd.h](lcd.h) |
| File-change evidence | [file_incremental_cache.h](file_incremental_cache.h), [scan_idle_gate.h](scan_idle_gate.h) |

Feature headers and the selected build profile control which code is compiled.
A header being present does not mean its feature is enabled in a release.
See the [developer guide](../docs/DEVELOPER_GUIDE.md) for build and test selection.
