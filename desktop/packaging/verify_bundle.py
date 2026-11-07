"""Check release-critical assets without contacting hardware or a network."""

from pathlib import Path
import argparse
import sys


def verify(bundle):
    bundle = Path(bundle)
    executable = bundle / ("cloud-stick.exe" if sys.platform == "win32" else "cloud-stick")
    needed = [executable, bundle / "_internal" / "frontend" / "index.html",
              bundle / 'firmware-helper' / ('cloud-stick-installer.exe' if sys.platform == 'win32' else 'cloud-stick-installer')]
    from verify_firmware import verify_firmware
    verify_firmware(bundle / '_internal' / 'firmware')
    missing = [str(path) for path in needed if not path.is_file()]
    for name in ("QtWebEngineProcess*", "icudtl.dat", "qtwebengine_resources.pak", "qtwebengine_resources_100p.pak", "qtwebengine_resources_200p.pak"):
        if not any(path.is_file() for path in bundle.rglob(name)):
            missing.append(name)
    if not any(path.is_file() for path in bundle.rglob("qtwebengine_locales/en-US.pak")):
        missing.append("qtwebengine_locales/en-US.pak")
    if missing:
        raise SystemExit("Bundle is incomplete: " + ", ".join(missing))
    print("Bundle assets verified: executable, local UI, QtWebEngine helper, resources, ICU, locales")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    verify(parser.parse_args().bundle)

