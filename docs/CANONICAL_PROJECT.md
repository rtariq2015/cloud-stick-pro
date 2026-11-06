# Cloud Stick Pro project workspace

Use the dedicated Cloud Stick Pro folder for future development. On the current computer it is `~/Projects/cloud-stick-pro`. It contains this project only; Unmanic is not part of the product or required to run either app.

Open cloud-stick-pro.code-workspace in VS Code, or open the project folder directly. The workspace lists only the current folder. It recommends Python, C++ and PlatformIO extensions without installing them, changes only project-local file/search exclusions, and defines no automatic build, flash or debug tasks. VS Code documents the workspace format and settings in its [workspace guide](https://code.visualstudio.com/docs/editing/workspaces/workspaces).

## Where to work

| Folder | Purpose |
|---|---|
| src/, include/, lib/, boards/, sdk/ | Firmware, board description and reviewed SDK integration sources |
| desktop/ | Linux/Windows setup app, bundled interface, tests and packaging recipes |
| android/ | Local Android viewer, tests and APK build recipe |
| docs/ | User/developer guides, credits and ordered release notes |
| test/, tools/ | Firmware/native regressions, guarded build and source-export tools |
| releases/2026-11-06-current/ | Local installers, app images and sanitized review evidence; ignored by Git |

The releases folder remains visible in VS Code but is excluded from source searches and file watching. Original development worktrees and recovery material are preserved separately; they are not a second active source tree.

## Source and GitHub

The intended origin is [rtariq2015/cloud-stick-pro](https://github.com/rtariq2015/cloud-stick-pro). The new local development branch can track origin/main for comparison while retaining its source changes locally. Setting a remote or fetching it does not publish local files. Do not put tokens in remote URLs.

The public GitHub repository contains the complete reviewed development source, tests, guides, previews and maintenance workflows. Download it with Code → Download ZIP, or clone/pull the repository. Packaged installer downloads are separate; no general binary release is claimed. Keep [upstream attribution and the unresolved license evidence](CREDITS.md), and preserve all component notices. The source snapshot manifest records the reviewed local source commit; the publishing commit is a separate identity.

## Builds and dependencies

Opening this workspace does not download dependencies, rebuild firmware or alter the device. Dependencies and private configuration belong outside the source snapshot or in ignored local build directories.

The ordinary PlatformIO default profile is a separate BLE-disabled, incremental-cache-disabled build. It is not the reviewed product image currently used on the dongle. Do not select the default profile and assume it reproduces the installed 0.4.3-ui-dev release.

The reviewed product profile uses custom SDK memory settings, pinned libraries/compilers and frozen reference files. The guarded builder currently retains machine-local dependency paths outside this project. Moving the source does not migrate those caches. Current firmware and desktop builds do not require the old private Git history.

Before rebuilding a product release here, bind the retained pinned SDK/toolchain and reference files, then run the guard checks with this folder as the source root. No task silently builds the old worktree or substitutes a cached binary. The release report identifies the exact source used for the included binaries.

For the setup apps, follow the pinned environments and packaging instructions in [desktop/README.md](../desktop/README.md) and [android/README.md](../android/README.md). Linux and Windows use the same desktop interface source, but Windows executables must be built and tested on Windows. A Linux build does not validate a Windows installer. Android release signing uses the existing private signing material outside the repository.

Some preserved component guides include historical container-wrapper examples from the original development machine. Those examples describe that retained environment; they do not make Unmanic a dependency of this independent project. Prefer the component-relative source/build instructions, and verify external dependency bindings before any product build.

## Private material and device operations

Keep owner cards, passwords, vaults, signing keys, live flash/NVS copies, raw operator logs and user/QA files outside this repository and outside the local release folder. Git ignore rules are a convenience, not a secret scanner. Review staged files before every publication.

Firmware is installed on internal flash; the microSD stores user files. A normal source edit or app build is not a device operation. Any installation uses the exact reviewed app image, private current recovery checkpoint, storage-ownership checks and app-only procedure. Do not write a partition table, initialize LittleFS, erase all flash or format the card as part of workspace setup.

See [Getting started](GETTING_STARTED.md), [release notes](RELEASES.md), [project history](PROJECT_HISTORY.md) and [storage safety](STORAGE_SAFETY.md) for the corresponding user workflow and evidence limits.
