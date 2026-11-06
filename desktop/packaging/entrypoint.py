"""Frozen application entry point; source launch remains python -m cloudstick_desktop."""

from cloudstick_desktop.main import main

if __name__ == "__main__":
    raise SystemExit(main())

