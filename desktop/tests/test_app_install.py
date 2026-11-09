# SPDX-License-Identifier: MIT
"""Reversible user-program replacement; synthetic folders and no child process."""
import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch,MagicMock
from cloudstick_desktop import app_install

@unittest.skipUnless(sys.platform=='linux','Linux user-install replacement')
class UserReplacementTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.home=Path(self.temp.name);self.old=self.home/'.local/opt/cloud-stick-desktop';self.old.mkdir(parents=True)
        (self.old/'.cloud-stick-user-install').write_text('cloud-stick-desktop\n');(self.old/'cloud-stick').write_text('old program')
        self.new=self.home/'download/release/cloud-stick';self.new.mkdir(parents=True);(self.new/'cloud-stick').write_text('new program')
        (self.new.parent/'uninstall-user.py').write_text('new uninstaller')
        self.preferences=self.home/'.local/share/preferences';self.preferences.parent.mkdir(parents=True);self.preferences.write_text('synthetic retained preference')
    def scope(self):
        return patch.multiple(app_install.sys,frozen=True,executable=str(self.new/'cloud-stick'),create=True)
    def test_success_retains_previous_program_and_preferences(self):
        with self.scope(),patch.object(Path,'home',return_value=self.home),patch.object(app_install.os,'kill',side_effect=ProcessLookupError),patch.object(app_install.subprocess,'Popen') as launch:
            self.assertEqual(app_install.replace_user_install(1234),0)
        self.assertEqual((self.old/'cloud-stick').read_text(),'new program')
        self.assertEqual((self.old/'uninstall-user.py').read_text(),'new uninstaller')
        backups=list(self.old.parent.glob('cloud-stick-desktop-before-*'));self.assertEqual(len(backups),1)
        self.assertEqual((backups[0]/'cloud-stick').read_text(),'old program');self.assertEqual(self.preferences.read_text(),'synthetic retained preference')
        self.assertEqual(launch.call_args.args[0],[str(self.old/'cloud-stick')])
    def test_failed_swap_restores_previous_program_before_cleanup(self):
        original=Path.rename
        def rename(path,target):
            if path.name.startswith('.cloud-stick-update-') and Path(target)==self.old:raise OSError('synthetic replacement failure')
            return original(path,target)
        with self.scope(),patch.object(Path,'home',return_value=self.home),patch.object(app_install.os,'kill',side_effect=ProcessLookupError),patch.object(Path,'rename',rename),patch.object(app_install.subprocess,'Popen') as launch:
            with self.assertRaises(OSError):app_install.replace_user_install(1234)
        self.assertEqual((self.old/'cloud-stick').read_text(),'old program');self.assertFalse(list(self.old.parent.glob('.cloud-stick-update-*')));launch.assert_not_called()
    def test_archive_escape_is_rejected_before_installer_launch_or_program_change(self):
        archive=self.home/'download/update.tar.gz'
        with tarfile.open(archive,'w:gz') as out:
            entry=tarfile.TarInfo('../outside');entry.size=1;out.addfile(entry,io.BytesIO(b'x'))
        with patch.object(Path,'home',return_value=self.home),patch.object(app_install.sys,'executable',str(self.old/'cloud-stick')),patch.object(app_install.subprocess,'Popen') as launch:
            with self.assertRaises(Exception):app_install.prepare_user_update(archive,{'version':'0.7.0','source':'0'*40})
        self.assertFalse((archive.parent/'outside').exists());self.assertFalse((self.home/'outside').exists());self.assertEqual((self.old/'cloud-stick').read_text(),'old program');launch.assert_not_called()
