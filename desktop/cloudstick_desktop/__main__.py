"""App entry: private installer workers never initialize Qt or Bluetooth."""
import sys
if len(sys.argv) > 1 and sys.argv[1] == '--firmware-worker':
    from cloudstick_desktop.firmware_worker import main
else:
    from cloudstick_desktop.main import main
if __name__ == '__main__':
    raise SystemExit(main())
