# Cloud Stick Setup

A native Linux/Windows application for Cloud Stick Pro. A PySide6 window renders
the bundled setup interface through a private `cloudstick://app` scheme;
Bleak performs Bluetooth operations using the operating system's Bluetooth
stack. The interface requires no hosted setup site, Web Bluetooth browser, or
local HTTP server.

This release provides device discovery, the existing protected owner login,
Wi-Fi and Koofr settings, and device/backup status through the existing firmware
protocol. The USB panel lists ports and can request the existing setup window.
USB does not provide Wi-Fi, Koofr, or login settings. Those use Bluetooth.

USB owner setup and diagnostics accept only the reviewed `0.4.1-setup-dev` and
`0.4.2-incremental-dev`, `0.4.3-ui-dev`, `0.4.4-wifi-dev`, `0.4.5-ble-dev` and `0.4.6-cache-dev` firmware profiles. Other versions remain blocked.

After finding, selecting and checking the USB, choose **Troubleshoot / updates**
for an explicit **Diagnose my stick** check. Finish active setup first. The screen reads
one sequential local snapshot: Wi-Fi and owner state, recorded backup counts,
errors, memory, uptime and local dashboard address. It does not start an upload,
contact Koofr or inspect microSD files. Safe diagnostic JSON excludes addresses,
names, accounts, passwords, paths and file history. Private backup records still
require authenticated sign-in. No background diagnostic polling is added.

For a new login, `admin` is an allowed username. Choose your own memorable password
phrase of at least 12 English characters and repeat it. The app enforces its
existing 12–128 UTF-8 byte policy before Review; `admin` alone is too short.
Review only stages the change. Explicitly confirm and save, then reconnect with
your new login. Your previous login stays active until the save succeeds.

On this already trusted Linux computer, select and check the matching USB, then
click **Create my login**. The native app can use its protected local setup-access
reference once, bind the exact USB/Bluetooth identity, and take you directly to
choosing your username/password. The existing key is not put in a form field.
Review and explicit Save remain required. After confirmed new-login reconnect
and Finish, the app retires the old local reference while retaining the original
user file. A cancelled or uncertain save does not retire it.

This is remembered authorization for an existing owned stick. It does not claim
an unowned device, reset ownership, or let an unconfigured computer replace a
login. Without matching saved access, use your existing username/password or the
advanced legacy-key fallback. First-owner creation is available only on reviewed firmware reporting a genuinely
Missing owner. It requires SD absent, the actual continuous ten-second BOOT hold,
Review and explicit Create account, then a fresh chosen-login proof. Ready/Fault
records cannot be overwritten. Unknown outcomes cannot be retried automatically.
The 0.6.0 preview adds a separate guarded installer for recognized original boards;
unknown firmware remains blocked. Windows saved-access handling fails
closed until a native equivalent is implemented and tested.

**The 0.6.0 preview adds firmware installation on Windows and Linux.** Start at
**Install or update your stick**, then continue through the same login/Wi-Fi/cloud
wizard. The installer includes a signed offline firmware package, checks for newer
signed releases, makes private encrypted recovery copies and verifies read-back.
Existing sticks use an app-only update; new original boards use a distinct guarded
first installation. See the [plain-language guide](../docs/INTEGRATED_SETUP.md).
New-board physical acceptance is pending; no owned stick was erased to simulate it.
The 0.5.7 release remains the previous diagnostics/manifest-review-only app.

## Install a Linux release

The initial Linux build targets Ubuntu 24.04 x86-64 / glibc 2.39 and newer
compatible desktop systems. It includes Python, Qt, QtWebEngine and the Python
dependencies. The computer still needs its normal graphics/audio libraries,
D-Bus, a working Bluetooth adapter, and BlueZ for device operations. Run the app
as your ordinary desktop user with Chromium sandbox support enabled.

For the `.deb`, open `cloud-stick_0.6.0_amd64.deb` with your distribution's
package installer. Its package metadata declares the required system libraries.
Launch **Cloud Stick Setup** from the applications menu. Remove it through the same
package manager.

The explicit Ubuntu system-library list is
`packaging/linux/runtime-packages.txt`, also included as `runtime-packages.txt`
in the portable archive. This includes the Qt XCB/XKB/GL prerequisites that a
minimal build container may lack. An offscreen synthetic smoke test does not
validate an X11/Wayland desktop installation or its graphics drivers.

The `.deb` also installs `70-cloud-stick-pro.rules` under
`/usr/lib/udev/rules.d`. It grants USB serial access through `uaccess` only to
the active local desktop user for Espressif `303a:1001` tty devices. Installation
reloads the rules and requests a change event for existing `ttyACM*` ports; it
does not reset the stick. No world-writable mode or broad `dialout` membership
is added. If USB reports permission denied, install the `.deb` through your
system package installer and retry from your active desktop session. The
portable archive includes the same rule for administrator installation;
`install-user.py` does not install system rules. Bluetooth has its separate
normal BlueZ/session permissions.

For the portable archive, extract `cloud-stick-0.5.7-linux-x86_64.tar.gz` in a
writable directory. Open `cloud-stick/cloud-stick` to run immediately. To add an
applications-menu entry for your user, run this command in the extracted release
directory:

```sh
python3 install-user.py
```

It copies the application to `~/.local/opt/cloud-stick-desktop`, creates a
launcher at `~/.local/bin/cloud-stick`, and adds the icon/menu entry under
`$XDG_DATA_HOME` or `~/.local/share`. It requests no administrator access, changes
no Bluetooth policy, and refuses to overwrite existing installations. To remove
that installation:

```sh
python3 ~/.local/opt/cloud-stick-desktop/uninstall-user.py
```

Keep the complete application directory together; `_internal` contains the
engine, libraries and interface. Copying only the executable is insufficient.

To replace an existing current-user installation manually, first quit the app
and verify that `.cloud-stick-user-install` inside its directory contains
`cloud-stick-desktop`. Check that the existing launcher and menu entry refer to
that same directory. Keep a private backup of the complete application directory,
`~/.local/bin/cloud-stick`, and the matching menu/icon files under
`$XDG_DATA_HOME` (or `~/.local/share`). Move those four owned paths into that
backup on the same filesystem, then run the new release's `install-user.py`.
Launch **Cloud Stick Setup** and retain the backup until it works. If replacement
fails, close the new app, remove only its verified same-app installation, and
restore all four original paths. The installer deliberately refuses existing
paths; it does not perform an automatic update or overwrite an older version.

## Install a Windows release

The release recipe targets Windows 11 x64 with a Bluetooth adapter. Extract the
entire Windows ZIP, then open `cloud-stick\cloud-stick.exe`. If a Windows builder
also produced the optional `*-setup.exe`, it installs for your current user and
adds a Start-menu shortcut. The installer does not require administrator access.

Windows ZIP/installer files must be produced on Windows. A Linux build does not
validate the Windows executable, native Bluetooth backend, or installer.
Unsigned development artifacts may show the usual Windows publisher prompt.

## First connection

Open the app, activate the stick's existing setup window, and choose **Find
Cloud Stick**. Enter the login already registered for the stick, or expand the
existing private-key option. The app does not provide a factory `admin/admin`
login. Review settings in the interface before explicitly saving changes.

The native window allows only bundled interface resources. It keeps the page
profile in memory and blocks remote resources. Explicit external links ask
before opening your usual browser. Closing the window ends the native session.

## Run from source

Use CPython 3.12 and a fresh virtual environment. All release runtime/build
dependencies, including the Qt components and platform-specific native backend,
are pinned in `requirements.txt`. Source launch is from the `desktop` directory:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python -m cloudstick_desktop
```

On Windows, use `.venv\Scripts\python.exe` in those commands. The supported
release format is the PyInstaller bundle; Python wheel distribution is not a
release artifact. The checkout keeps the frontend beside the Python package.

In this repository's development environment, all commands run through the
workspace wrapper. For example, from the workspace root:

```sh
./compose.sh -f docker-compose.yml -f build/research/cloud-stick-auth-cleanup-compose.yml \
  exec -T --user 1000:1000 \
  --workdir /home/home/tool.plugin-builder/build/research/cloud-stick-desktop-app/desktop \
  unmanic-dev .venv/bin/python -m cloudstick_desktop --smoke-test \
  --report /tmp/cloud-stick-smoke.json --screenshot /tmp/cloud-stick-smoke.png
```

The smoke mode uses a synthetic transport. It opens the real native window and
bundled UI without discovering, connecting to, or opening physical devices.
It still requires a working graphical session, or a suitable virtual display
with Chromium sandbox support. Build scripts never disable the sandbox.

## Build Linux artifacts

Prepare the pinned environment above on the Ubuntu 24.04 x86-64 baseline. From
`desktop`, run:

```sh
CLOUDSTICK_PYTHON=.venv/bin/python sh tools/build-linux.sh
```

Use the workspace wrapper for this command in the repository development
environment. The script checks dependencies, builds a PyInstaller **onedir**
application, validates the bundled frontend/WebEngine helper/resources/locales,
then emits a portable archive, `.deb`, and `SHA256SUMS` under `dist/releases`.
It writes only build output and does not install the result. `dpkg-deb` must
already be available for the `.deb`; pass `--no-deb` for the archive alone.

The spec relies on PyInstaller's maintained PySide6 QtWebEngine hook to preserve
the Qt directory layout and collect the helper process, ICU, resources, locales
and dependent libraries. It packages the complete local frontend and includes
the selected operating system's Bleak backend.

To smoke-test the built app without device access:

```sh
dist/cloud-stick/cloud-stick --smoke-test --report /tmp/cloud-stick-built.json \
  --screenshot /tmp/cloud-stick-built.png
```

## Build Windows artifacts on Windows

In PowerShell on Windows 11 x64, with CPython 3.12 installed:

```powershell
cd desktop
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\tools\build-windows.ps1 -Python .\.venv\Scripts\python.exe
.\dist\cloud-stick\cloud-stick.exe --smoke-test --report "$env:TEMP\cloud-stick-smoke.json" --screenshot "$env:TEMP\cloud-stick-smoke.png"
```

For an optional per-user installer, install Inno Setup 6 on that build machine
and pass its compiler path:

```powershell
.\tools\build-windows.ps1 -Python .\.venv\Scripts\python.exe `
  -InnoSetupCompiler 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

The installer definition is `packaging/windows/cloud-stick.iss`. A manual
GitHub Actions example is in `packaging/workflows/windows-build.yml`; it is kept
outside `.github/workflows` so this change enables no remote automation. Copying
or enabling it, publishing artifacts and signing a release are separate release
actions.

## Release evidence and reproducibility

Each bundle includes `build-manifest.json` recording the Python version, build
OS, architecture, installed package versions and `SOURCE_DATE_EPOCH`. Archive
ordering, owners and timestamps are normalized. Set `SOURCE_DATE_EPOCH` to the
release commit time to reuse a release's timestamp. The package pins and recipe
make build inputs reviewable; they do not promise identical binary hashes across
different Python patches, OS images, system libraries or signing environments.

Before publishing, preserve the exact Python 3.12 patch, OS image and wheel
inputs; run source tests, build verification and the synthetic native-window
smoke test on each target OS. Device/cloud acceptance requires a separately
authorized test with a real stick. A successful synthetic smoke test does not
prove a Bluetooth adapter, physical login, settings write, cloud backup or
firmware update works.

The runtime uses the Qt 6.10.3 package layout and keeps dependencies dynamically
linked in the onedir bundle. Preserve included metadata and
`packaging/THIRD_PARTY_NOTICES.md` when distributing it.

Primary references checked for the packaging configuration:

- [PySide6 6.10.3 package metadata](https://pypi.org/project/PySide6/6.10.3/)
- [Bleak package and platform dependencies](https://pypi.org/project/bleak/3.0.2/)
- [Bleak Linux backend](https://bleak.readthedocs.io/en/latest/backends/linux.html)
- [pyserial 3.5](https://pypi.org/project/pyserial/3.5/)
- [PyInstaller native build and onedir model](https://pyinstaller.org/en/stable/operating-mode.html)
- [PyInstaller 6.22.3 QtWebEngine hook](https://github.com/pyinstaller/pyinstaller/blob/v6.22.3/PyInstaller/hooks/hook-PySide6.QtWebEngineCore.py)
- [Inno Setup command-line compiler](https://jrsoftware.org/ishelp/index.php?topic=compilercmdline)


### Diagnose my stick

Under **Troubleshoot / updates**, choose **Diagnose my stick** after closing
setup. It explains the reported state and a useful next step, including when
Bluetooth cleanup has left backups paused. Use **Copy safe diagnostic JSON**
when a problem remains. Each click reads one local snapshot; it does not make a
repair, test the cloud account or prove file integrity.
