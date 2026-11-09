# SPDX-License-Identifier: MIT
"""Install verified companion releases with OS approval or a reversible user swap."""
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time


def clean_environment():
    env = dict(os.environ)
    for name in ('LD_LIBRARY_PATH', 'LIBPATH'):
        original = env.pop(name + '_ORIG', None)
        if original is None:
            env.pop(name, None)
        else:
            env[name] = original
    env['PYINSTALLER_RESET_ENVIRONMENT'] = '1'
    return env


def prepare_user_update(archive, release):
    destination = Path.home() / '.local/opt/cloud-stick-desktop'
    if (destination.is_symlink() or not (destination / '.cloud-stick-user-install').is_file()
            or (destination / '.cloud-stick-user-install').read_text() != 'cloud-stick-desktop\n'
            or Path(sys.executable).resolve().parent != destination.resolve()):
        raise ValueError('app_install_location')
    stage = Path(tempfile.mkdtemp(prefix='verified-app-', dir=archive.parent))
    try:
        with tarfile.open(archive, 'r:gz') as source:
            entries = source.getmembers()
            if len(entries) > 30000 or sum(x.size for x in entries) > 2 * 1024**3:
                raise ValueError('app_archive_invalid')
            tops = {PurePosixPath(x.name).parts[0] for x in entries if PurePosixPath(x.name).parts}
            if len(tops) != 1:
                raise ValueError('app_archive_invalid')
            source.extractall(stage, members=entries, filter='data')
        root = stage / next(iter(tops)) / 'cloud-stick'
        executable = root / 'cloud-stick'
        if not executable.is_file() or executable.is_symlink():
            raise ValueError('app_archive_invalid')
        import json
        manifest = json.loads((root / 'build-manifest.json').read_text())
        if manifest['version'] != release['version'] or manifest['source_commit'] != release['source'] or manifest['desktop_source_dirty']:
            raise ValueError('app_archive_identity')
        subprocess.Popen([str(executable), '--replace-user-install', str(os.getpid())],
                         env=clean_environment(), start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except BaseException:
        shutil.rmtree(stage)
        raise


def replace_user_install(pid):
    """Runs from the newly extracted executable, outside the old install tree."""
    if sys.platform != 'linux' or not getattr(sys, 'frozen', False) or pid <= 1:
        return 1
    source = Path(sys.executable).resolve().parent
    destination = Path.home() / '.local/opt/cloud-stick-desktop'
    if source == destination or destination.is_symlink() or not (destination / '.cloud-stick-user-install').is_file():
        return 1
    for _ in range(600):
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            break
        time.sleep(.1)
    else:
        return 1
    stage = Path(tempfile.mkdtemp(prefix='.cloud-stick-update-', dir=destination.parent))
    backup = destination.with_name('cloud-stick-desktop-before-' + str(time.time_ns()))
    try:
        shutil.copytree(source, stage, dirs_exist_ok=True, symlinks=True)
        (stage / '.cloud-stick-user-install').write_text('cloud-stick-desktop\n')
        # Retain the reviewed uninstaller, launcher and preferences.
        shutil.copy2(source.parent / 'uninstall-user.py', stage / 'uninstall-user.py')
        destination.rename(backup)
        try:
            stage.rename(destination)
        except BaseException:
            backup.rename(destination)
            raise
        subprocess.Popen([str(destination / 'cloud-stick')], env=clean_environment(), start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return 0
    finally:
        if stage.exists():
            shutil.rmtree(stage)
