"""Native GUI entry point. The independent installer is a separate program."""
if __name__ == '__main__':
    import sys
    if len(sys.argv) == 3 and sys.argv[1] == '--replace-user-install':
        from cloudstick_desktop.app_install import replace_user_install
        raise SystemExit(replace_user_install(int(sys.argv[2])))
    if sys.argv[1:] == ['--disable-startup']:
        from cloudstick_desktop.startup import configure
        configure(False)
        import time
        time.sleep(5)
        raise SystemExit(0)
    if sys.argv[1:] == ['--watch']:
        from cloudstick_desktop.startup import watch
        raise SystemExit(watch())
    from cloudstick_desktop.main import main
    raise SystemExit(main())
