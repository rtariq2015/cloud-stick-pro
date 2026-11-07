# Pinned framework inputs

These folders preserve reviewed reference sources and patches. Keep original
notices, checksums and patch records together; they are part of the build review.

| Folder | Purpose |
| --- | --- |
| [pinned_ble](pinned_ble/README.md) | Espressif provisioning/Security2 and Bluetooth lifecycle integration inputs |
| [pinned_nimble_stop](pinned_nimble_stop/README.md) | Reviewed NimBLE shutdown patch and its original reference |
| [pinned_http_upload](pinned_http_upload/HTTPClient.cpp.orig) | HTTPClient reference and reviewed upload adaptation |

The guarded product build also checks an external frozen SDK. That build input
is separate from these small source references. Follow the
[developer guide](../docs/DEVELOPER_GUIDE.md) and retain the exact version/hash
record instead of modifying a frozen SDK to get a build passing.
