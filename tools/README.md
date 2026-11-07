# Build and review tools

Everyday setup uses [Cloud Stick Setup](../desktop/README.md). These scripts are
for maintainers; the table separates source/package work from device work.

| Goal | Entry point | What it touches |
| --- | --- | --- |
| Export reviewed public source | [export_public_project.py](export_public_project.py) | Committed Git blobs and a fresh output directory; excludes private data |
| Check/build a guarded firmware profile | [build_public_wifi_memory_profile.py](build_public_wifi_memory_profile.py) | Explicit source, profile and pinned SDK inputs; build is separate from upload |
| Make signed install data | [build_firmware_install_bundle.py](build_firmware_install_bundle.py) | Exact reviewed public images and an external private release key; no dongle |
| Make/check the empty first-install journal | [create_empty_journal.py](create_empty_journal.py), [verify_empty_journal.c](verify_empty_journal.c) | Generated public image and pinned LittleFS source; no microSD |
| Check saved Wi-Fi policy | [test_wifi_reconnect.py](test_wifi_reconnect.py) | Native simulation |
| Check NVS cache cleanup | [test_nvs_cache_release.py](test_nvs_cache_release.py) | Synthetic SDK/flash fixture |
| Read a physical device's status | [device_status.py](device_status.py) | Selected serial device; requires separate device scope and ownership checks |

Other numbered/milestone tools retain their original review scope. They are not
interchangeable generic installers. Use the current [developer guide](../docs/DEVELOPER_GUIDE.md),
[maintenance process](../docs/MAINTENANCE.md) and exact release evidence before
choosing one. Private backups, signing keys and raw device logs are never public
source or release inputs.
