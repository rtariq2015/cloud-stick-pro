# Native app packaging

The active native build is
[desktop-native.yml](../../.github/workflows/desktop-native.yml). It builds on
Linux and Windows, verifies the signed offline firmware data, runs software/fault
tests, checks both packaged programs and exercises a Windows per-user install,
launch and uninstall. It never flashes hardware.

| File / folder | Purpose |
| --- | --- |
| [cloud-stick.spec](cloud-stick.spec) | Qt GUI and its local interface |
| [firmware-helper.spec](firmware-helper.spec) | Independent installer program, with no Qt frontend |
| [verify_firmware.py](verify_firmware.py) | Signature, size, membership and image-hash checks before packaging |
| [verify_bundle.py](verify_bundle.py) | Required executable, firmware data and Qt resources |
| [package_artifacts.py](package_artifacts.py) | Archives, `.deb`, build manifest, notices and checksums |
| [installer_source.py](installer_source.py) | Preferred installer source and dependency notices shipped beside it |
| [windows/cloud-stick.iss](windows/cloud-stick.iss) | Per-user Windows installer |
| [test-windows-install.ps1](test-windows-install.ps1) | Fresh CI installation, synthetic program checks and uninstall |
| [Linux helpers](linux/install-user.py) | Launcher, user-local helper, runtime dependencies and narrow USB access rule |
| [licenses](licenses/README.md) | Component texts and copied-source records |

Build commands and the exact offline-data download are in the
[desktop guide](../README.md). Preserve the [third-party notices](THIRD_PARTY_NOTICES.md)
and [project credits](../../docs/CREDITS.md). Source/binary review and physical
acceptance remain separate from a successful package build.


## Prepare a source build

Use CPython 3.12 and `desktop/requirements.txt` on the target OS. The complete
release also needs the reviewed offline `.cspfw` file in `desktop/firmware`.
Download it from the public
[firmware-data release](https://github.com/rtariq2015/cloud-stick-pro/releases/tag/firmware-data-0.4.6-r1),
then verify it against the signed catalog before building:

```sh
python desktop/packaging/verify_firmware.py desktop/firmware
```

This validates data only. It never contacts or writes a dongle. Generated bundles,
installers and private checkpoints do not belong in Git.

On Ubuntu 24.04 x86-64, from `desktop`, with the pinned environment installed:

```sh
CLOUDSTICK_PYTHON=.venv/bin/python sh tools/build-linux.sh
```

The result is a portable archive and `.deb` in `desktop/dist/releases`. `dpkg-deb`
and the listed system libraries must be available; `--no-deb` creates only the
archive. The script builds, checks and packages; it does not install or flash.

On Windows 11 x64, use PowerShell from `desktop`:

```powershell
.\tools\build-windows.ps1 -Python .\.venv\Scripts\python.exe `
  -InnoSetupCompiler 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

Builds must run on their target OS. Keep the GUI, independent installer,
`_internal`, source ZIP and notices together. `build-manifest.json` records the
exact source, interpreter, OS, packages and file hashes. The recipe normalizes
archive metadata; it does not promise identical binary hashes across different
Python patches or OS images. Native window smoke uses `--smoke-test`; its
transport is synthetic and its success is not physical hardware acceptance.
