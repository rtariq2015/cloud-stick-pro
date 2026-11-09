# Cloud Stick help

Start with what you want to do. The everyday guides use plain language; code and
release records have their own section below.

## Using your stick

| I want to… | Open this guide |
| --- | --- |
| Get everything working for the first time | [Start here](GETTING_STARTED.md) |
| Install firmware on a new original board | [One-app installation](INTEGRATED_SETUP.md) |
| Add home Wi-Fi or my phone hotspot | [Wi-Fi and hotspot setup](GETTING_STARTED.md#3-add-wi-fi-or-your-phone-hotspot) |
| Check live status on my Android phone | [Android dashboard and private history](../android/README.md) |
| Connect a trusted phone without holding BOOT | [Button-free connection](BUTTON_FREE_CONNECTION.md) |
| Get the newest computer app or firmware | [Updates](UPDATING.md) |
| Understand an error or a connection problem | [Troubleshooting](TROUBLESHOOTING.md) |
| See what changed | [Release notes](RELEASE_NOTES.md) |

The computer app is **Cloud Stick Setup**. The Android app is **Cloud Stick
Viewer**. Firmware is the software running inside the USB stick. Each has its
own version number.

## Working on the project

| Topic | Guide |
| --- | --- |
| Where the files live | [Project map](PROJECT_LAYOUT.md) |
| How to build and test a change | [Developer guide](DEVELOPER_GUIDE.md) |
| Safe USB and backup behavior | [Storage boundaries](STORAGE_SAFETY.md) |
| Upload speed work and measured limits | [Upload performance candidate](UPLOAD_PERFORMANCE.md) |
| Code folders | [Firmware](../src/README.md), [interfaces](../include/README.md), [desktop](../desktop/README.md), [Android](../android/README.md) |
| Checks and helper tools | [Tests](../test/README.md), [tools](../tools/README.md), [packaging](../desktop/packaging/README.md) |
| Pinned third-party code | [SDK inputs](../sdk/README.md), [libraries](../lib/README.md), [credits](CREDITS.md) |
| How updates get reviewed and saved | [Maintenance](MAINTENANCE.md) |

## Records and history

[Release records](RELEASES.md) link the exact component versions, file hashes and
what was actually tested. [Project history](PROJECT_HISTORY.md) keeps the ordered
milestones. [Dated reports](releases/2026-11-06-ble-cleanup.md) describe their own
versions; an older result is not a test of a new build.

The [historical source manifest](provenance/README.md) is preserved separately.
New release source archives include a fresh manifest for that archive. Private
recovery copies, signing keys and raw device logs stay outside public packages.
