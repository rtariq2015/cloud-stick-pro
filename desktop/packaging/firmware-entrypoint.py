# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent GPL installer program; no Qt, frontend or setup account code."""
from cloudstick_desktop.firmware_worker import main
if __name__ == '__main__':
    raise SystemExit(main())
