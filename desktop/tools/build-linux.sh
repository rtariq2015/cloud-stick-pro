#!/bin/sh
# Run on the Linux target baseline with desktop/requirements.txt installed.
set -eu
DESKTOP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_PYTHON=${CLOUDSTICK_PYTHON:-python3}
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
"$BUILD_PYTHON" "$DESKTOP_DIR/packaging/check_build_environment.py" linux
"$BUILD_PYTHON" -m pip check
"$BUILD_PYTHON" "$DESKTOP_DIR/packaging/verify_firmware.py" "$DESKTOP_DIR/firmware"
"$BUILD_PYTHON" -m PyInstaller --clean --noconfirm --distpath "$DESKTOP_DIR/dist" \
  --workpath "$DESKTOP_DIR/build/installer" "$DESKTOP_DIR/packaging/firmware-helper.spec"
"$BUILD_PYTHON" -m PyInstaller --clean --noconfirm \
  --distpath "$DESKTOP_DIR/dist" \
  --workpath "$DESKTOP_DIR/build/pyinstaller" \
  "$DESKTOP_DIR/packaging/cloud-stick.spec"
cp -a "$DESKTOP_DIR/dist/cloud-stick-installer" "$DESKTOP_DIR/dist/cloud-stick/firmware-helper"
"$BUILD_PYTHON" "$DESKTOP_DIR/packaging/package_artifacts.py" "$@"

