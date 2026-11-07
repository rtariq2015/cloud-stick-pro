# Cloud Stick Pro project workspace

Use the dedicated Cloud Stick Pro folder for future development. On the current computer it is `~/Projects/cloud-stick-pro`. It contains this project only; Unmanic is not part of the product or required to run either app.

Open cloud-stick-pro.code-workspace in VS Code, or open the project folder directly. The workspace lists only the current folder. It recommends Python, C++ and PlatformIO extensions without installing them. Project-local settings exclude build caches from searches and file watching, save edited files locally after 1.5 seconds, disable Git smart commits and retain the Sync confirmation. No automatic build, flash, debug, commit or push task is defined. VS Code documents the workspace format and settings in its [workspace guide](https://code.visualstudio.com/docs/editing/workspaces/workspaces) and [Auto Save guide](https://code.visualstudio.com/docs/editing/codebasics#_save-auto-save).

Auto Save writes files to this computer. Git commits record reviewed milestones;
successful pushes save those commits to GitHub. Follow the active-session
[progress and saving agreement](MAINTENANCE.md#progress-and-saving-verified-milestones).
The existing local `AGENTS.md` points agents to that agreement and stays excluded
from public packages.

## Where to work

| Folder | Purpose |
|---|---|
| src/, include/, lib/, boards/, sdk/ | Firmware, board description and reviewed SDK integration sources |
| desktop/ | Linux/Windows setup app, bundled interface, tests and packaging recipes |
| android/ | Local Android viewer, tests and APK build recipe |
| docs/ | User/developer guides, credits and ordered release notes |
| test/, tools/ | Firmware/native regressions, guarded build and source-export tools |
| releases/2026-11-07-ble-cache/ | Local installers, app images and sanitized review evidence; ignored by Git |

The releases folder remains visible in VS Code but is excluded from source searches and file watching. Original development worktrees and recovery material are preserved separately; they are not a second active source tree.

## Source and GitHub

The intended origin is [rtariq2015/cloud-stick-pro](https://github.com/rtariq2015/cloud-stick-pro). Local `main` tracks `origin/main`; a published development branch should track its own remote branch. Review any legacy development branch that tracks `origin/main` before pushing it. Setting a remote or fetching it does not publish local files. Do not put tokens in remote URLs.

The public GitHub repository contains the complete reviewed development source, tests, guides, previews and maintenance workflows. Download it with Code → Download ZIP, or clone/pull the repository. Packaged installer downloads are separate; no general binary release is claimed. Keep [upstream attribution and the unresolved license evidence](CREDITS.md), and preserve all component notices. The source snapshot manifest records the reviewed local source commit; the publishing commit is a separate identity.

### Terminal and editor diagnostics

The setup inspection found a session sandbox that denied DNS sockets. A GitHub
DNS error there does not establish a machine-wide DNS fault, and installing
GitHub CLI cannot remove that session restriction. Check terminal connectivity
and authentication in a normal VS Code terminal before relying on terminal
pushes. Subsequent reviewed development-branch and preview-tag terminal pushes
succeeded, and their PRs were merged with CI passing. A connected GitHub account
and a terminal credential helper are separate authentication paths. Never print tokens or embed them in a Git remote.

On this computer, the Snap launcher failed under that confined session, while
the installed CLI worked using `/snap/code/current/usr/share/code/bin/code`
with `--version`. This is a verified diagnostic invocation, not an AppArmor or
system-service repair. Do not disable confinement or add `--no-sandbox` as a
workspace fix. Normal terminal behavior still needs its own check.

GitHub Pages remains disabled. Publishing repository commits does not publish a
website; enabling Pages requires a separate explicit decision.

## Builds and dependencies

Opening this workspace does not download dependencies, rebuild firmware or alter the device. Dependencies and private configuration belong outside the source snapshot or in ignored local build directories.

The ordinary PlatformIO default profile is a separate BLE-disabled, incremental-cache-disabled build. It is not the reviewed product image currently used on the dongle. Do not select the default profile and assume it reproduces the installed 0.4.6-cache-dev release.

The reviewed product profile uses custom SDK memory settings, pinned libraries/compilers and frozen reference files. The guarded builder currently retains machine-local dependency paths outside this project. Moving the source does not migrate those caches. Current firmware and desktop builds do not require the old private Git history.

Before rebuilding a product release here, bind the retained pinned SDK/toolchain and reference files, then run the guard checks with this folder as the source root. No task silently builds the old worktree or substitutes a cached binary. The release report identifies the exact source used for the included binaries.

For the setup apps, follow the pinned environments and packaging instructions in [desktop/README.md](../desktop/README.md) and [android/README.md](../android/README.md). Linux and Windows use the same desktop interface source, but Windows executables must be built and tested on Windows. A Linux build does not validate a Windows installer. Android release signing uses the existing private signing material outside the repository.

Some preserved component guides include historical container-wrapper examples from the original development machine. Those examples describe that retained environment; they do not make Unmanic a dependency of this independent project. Prefer the component-relative source/build instructions, and verify external dependency bindings before any product build.

## Private material and device operations

Keep owner cards, passwords, vaults, signing keys, live flash/NVS copies, raw operator logs and user/QA files outside this repository and outside the local release folder. Git ignore rules are a convenience, not a secret scanner. Review staged files before every publication.

Ignore rules also cover environment variants, private-key formats, generated
disk images and named device backup dumps. Sanitized `.env.example`,
`.env.sample`, `.env.template` and similarly suffixed templates remain eligible
for explicit review, as do explicitly named public PEM certificates. Ignore
exceptions never bypass the publication allowlist. Do not copy live secrets into
examples or force-add a private image; preserve required source files and reviewed
fixtures when extending these rules.

Firmware is installed on internal flash; the microSD stores user files. A normal source edit or app build is not a device operation. Any installation uses the exact reviewed app image, private current recovery checkpoint, storage-ownership checks and app-only procedure. Do not write a partition table, initialize LittleFS, erase all flash or format the card as part of workspace setup.

See [Getting started](GETTING_STARTED.md), [release notes](RELEASES.md), [project history](PROJECT_HISTORY.md) and [storage safety](STORAGE_SAFETY.md) for the corresponding user workflow and evidence limits.
