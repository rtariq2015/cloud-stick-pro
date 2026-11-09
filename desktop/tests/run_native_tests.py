"""Native app tests; temporary credentials use a private umask on POSIX."""
from pathlib import Path
import os
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))


def flatten(suite):
    for test in suite:
        if isinstance(test, unittest.TestSuite): yield from flatten(test)
        else: yield test


def main():
    tests = list(flatten(unittest.defaultTestLoader.discover(str(Path(__file__).resolve().parent))))
    if sys.platform == 'win32':
        # Linux installer/permission fixtures exercise POSIX-specific APIs. Keep
        # those in Linux's full suite; run the real Windows protection cases here.
        names = ('test_app_updates.', 'test_app_install.', 'test_firmware_', 'test_shell.', 'test_transports.', 'test_enrollment.',
                 'test_diagnostics.', 'test_release_review.')
        tests = [test for test in tests if test.id().startswith(names)]
    normal = [t for t in tests if not t.id().startswith('test_setup_access.')]
    private = [t for t in tests if t.id().startswith('test_setup_access.')]
    runner = unittest.TextTestRunner(verbosity=2)
    okay = runner.run(unittest.TestSuite(normal)).wasSuccessful()
    if private:
        old = os.umask(0o077)
        try: okay = runner.run(unittest.TestSuite(private)).wasSuccessful() and okay
        finally: os.umask(old)
    return 0 if okay else 1


if __name__ == '__main__': raise SystemExit(main())
