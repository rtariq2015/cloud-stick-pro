"""Ship the independent GPL installer's preferred source beside its executable."""
from pathlib import Path
import importlib.metadata
import json
import zipfile


def stage_installer_source(bundle):
    desktop = Path(__file__).resolve().parents[1]
    names = ('firmware_worker.py', 'firmware_engine.py', 'firmware_usb.py',
             'firmware_catalog.py', 'firmware_platform.py', 'firmware_recovery.py')
    target = Path(bundle) / 'firmware-helper' / 'installer-source.zip'
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for name in names:
            z.write(desktop / 'cloudstick_desktop' / name, 'desktop/cloudstick_desktop/' + name)
        z.writestr('desktop/cloudstick_desktop/__init__.py', '')
        z.write(desktop / 'requirements.txt', 'desktop/requirements.txt')
        for name in ('firmware-entrypoint.py','firmware-helper.spec'):
            z.write(desktop / 'packaging' / name, 'desktop/packaging/' + name)
        for path in (desktop / 'packaging/licenses/firmware-installer').iterdir():
            z.write(path, 'licenses/' + path.name)
        # Unmodified esptool is pure Python. Include its actual installed source,
        # package metadata and build configuration alongside the app wrapper.
        for name, module in [('esptool', 'esptool'), ('esp-pylib', 'esp_pylib')]:
            dist = importlib.metadata.distribution(name)
            for entry in dist.files or ():
                if '..' in entry.parts or entry.suffix == '.pyc' or '__pycache__' in entry.parts:
                    continue
                source = Path(dist.locate_file(entry))
                if source.is_file() and (entry.parts[0] == module or entry.parts[0].endswith('.dist-info')):
                    z.write(source, 'dependencies/' + str(entry).replace('\\','/'))
        z.writestr('BUILD.txt', 'Independent installer: GPL-3.0-or-later; esptool: GPL-2.0-or-later.\n'
                   'Shared newly authored MIT modules have their own notice. Other component terms remain unchanged.\n'
                   'Use CPython 3.12 and desktop/requirements.txt on the target OS.\n'
                   'python -m PyInstaller --clean --noconfirm desktop/packaging/firmware-helper.spec\n'
                   'Exact dependency sources: https://pypi.org/project/esptool/5.4.0/#files and https://pypi.org/project/esp-pylib/1.2.1/#files\n'
                   'The desktop GUI and firmware data are aggregated with this separate executable.\n')
