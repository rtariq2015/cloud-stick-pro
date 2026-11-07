"""Check the offline installer inputs before packaging; no device/network."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cloudstick_desktop.firmware_catalog import load_catalog, load_bundle, BUNDLE_LIMIT


def verify_firmware(root):
    root = Path(root)
    catalog = load_catalog((root / 'catalog.json').read_bytes())
    path = root / catalog.latest.asset
    if not path.is_file() or path.stat().st_size > BUNDLE_LIMIT:
        raise SystemExit('Missing reviewed offline firmware package')
    load_bundle(path.read_bytes(), catalog.latest)
    print('Signed offline firmware catalog and all six images verified')


if __name__ == '__main__': verify_firmware(sys.argv[1])
