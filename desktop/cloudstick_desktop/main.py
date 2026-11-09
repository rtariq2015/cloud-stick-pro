"""Local-only Qt shell. No HTTP listener, web service, telemetry or auto-connect."""

import argparse
import asyncio
import json
from pathlib import Path
import re
import sys
import threading

from PySide6.QtCore import (QBuffer, QByteArray, QCoreApplication, QFile, QIODevice,
                            QObject, QThread, QTimer, QUrl, QLockFile, Signal, Slot)
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import QApplication, QFileDialog, QMainWindow, QMessageBox
from PySide6.QtWebChannel import QWebChannel
from PySide6.QtWebEngineCore import (QWebEnginePage, QWebEngineProfile,
                                    QWebEngineSettings, QWebEngineUrlRequestInterceptor,
                                    QWebEngineUrlRequestJob, QWebEngineUrlScheme,
                                    QWebEngineUrlSchemeHandler)
from PySide6.QtWebEngineWidgets import QWebEngineView

from . import __version__
from .release_review import review_manifest


def frontend_root():
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1])) / "frontend"


def reportable_response(raw):
    """Synthetic reports must never retain an explicit bootstrap credential."""
    value = json.loads(raw)
    if (isinstance(value, dict) and isinstance(value.get("result"), dict)
            and {"key", "record_hex", "record", "verifier", "salt", "password"}.intersection(value["result"])):
        return {"id": value.get("id"), "ok": False, "error": {"code": "sensitive_response_omitted"}}
    return value


def register_scheme():
    scheme = QWebEngineUrlScheme(b"cloudstick")
    scheme.setSyntax(QWebEngineUrlScheme.Syntax.Host)
    scheme.setFlags(QWebEngineUrlScheme.Flag.SecureScheme |
                    QWebEngineUrlScheme.Flag.CorsEnabled |
                    QWebEngineUrlScheme.Flag.FetchApiAllowed)
    QWebEngineUrlScheme.registerScheme(scheme)


class LocalAssets(QWebEngineUrlSchemeHandler):
    """Serve only explicit packaged assets, never arbitrary local filesystem URLs."""

    def __init__(self, root, parent=None):
        super().__init__(parent)
        self.root = root.resolve()
        self.allowed = {p.relative_to(root).as_posix(): p for p in root.rglob("*")
                        if p.is_file() and p.suffix in (".html", ".css", ".mjs", ".js")}

    def requestStarted(self, job):
        url = job.requestUrl()
        name = url.path().lstrip("/") or "index.html"
        if (job.requestMethod() != b"GET" or url.host() != "app" or url.hasQuery()
                or url.hasFragment() or name not in self.allowed and name != "qwebchannel.js"):
            job.fail(QWebEngineUrlRequestJob.Error.RequestDenied)
            return
        if name == "qwebchannel.js":
            source = QFile(":/qtwebchannel/qwebchannel.js")
            if not source.open(QIODevice.OpenModeFlag.ReadOnly):
                job.fail(QWebEngineUrlRequestJob.Error.UrlNotFound)
                return
            payload = bytes(source.readAll())
            source.close()
        else:
            target = self.allowed[name]
            if target.resolve().parent != self.root and self.root not in target.resolve().parents:
                job.fail(QWebEngineUrlRequestJob.Error.RequestDenied)
                return
            payload = target.read_bytes()
        mime = {".html": b"text/html", ".css": b"text/css", ".js": b"text/javascript",
                ".mjs": b"text/javascript"}.get(Path(name).suffix, b"application/octet-stream")
        buf = QBuffer(job)
        buf.setData(QByteArray(payload))
        buf.open(QIODevice.OpenModeFlag.ReadOnly)
        job.reply(QByteArray(mime), buf)


class LocalOnly(QWebEngineUrlRequestInterceptor):
    def interceptRequest(self, info):
        url = info.requestUrl()
        info.block(not (url.scheme() == "cloudstick" and url.host() == "app"))


class LocalPage(QWebEnginePage):
    def acceptNavigationRequest(self, url, kind, is_main_frame):
        if url.scheme() == "cloudstick" and url.host() == "app":
            return True
        if (is_main_frame and kind == self.NavigationType.NavigationTypeLinkClicked
                and url.scheme() in ("https", "http") and url.host()
                and not url.userName() and not url.password()):
            decision = QMessageBox.question(self.parent(), "Open your browser?",
                                           "Open this page in your usual browser?\n" + url.toDisplayString())
            if decision == QMessageBox.StandardButton.Yes:
                QDesktopServices.openUrl(url)
        return False

    def createWindow(self, kind):
        # No second page can inherit the native bridge. External links must be
        # handled by the explicit same-window navigation confirmation above.
        return None

    def javaScriptConsoleMessage(self, level, message, line, source):
        # Console/error text may contain entered secrets. Never persist it.
        if getattr(self, "synthetic_console", None) is not None:
            self.synthetic_console.append(message[:500])


class Worker(QThread):
    response = Signal(str)
    event = Signal(str)

    def __init__(self, factory=None, parent=None):
        super().__init__(parent)
        self.factory = factory
        self.loop = None
        self.transport = None
        self.ready = threading.Event()
        self.closing = False
        self._shutdown_started = False
        self.jobs = set()
        self.firmware_launches = set()

    def run(self):
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)
        try:
            if self.factory is None:
                from .transports import DesktopTransport
                self.transport = DesktopTransport(event_sink=self.emit_event)
            else:
                self.transport = self.factory(self.emit_event)
            self.ready.set()
            # closeEvent may run while the factory is initializing. Its earlier
            # stop request must survive the ready handoff.
            if self.closing:
                self.loop.create_task(self.shutdown())
            self.loop.run_forever()
        except Exception:
            self.emit_event({"type": "startup_failed"})
        finally:
            self.ready.set()
            pending = asyncio.all_tasks(self.loop)
            for task in pending:
                task.cancel()
            if pending:
                self.loop.run_until_complete(asyncio.gather(*pending, return_exceptions=True))
            self.loop.run_until_complete(self.loop.shutdown_asyncgens())
            self.loop.run_until_complete(self.loop.shutdown_default_executor(timeout=10))
            self.loop.close()

    def emit_event(self, value):
        # Native events are generated from allowlisted status, never payloads.
        self.event.emit(json.dumps(value))

    def submit(self, request, *, firmware_approval=None):
        if (self.closing or not self.ready.is_set() or self.loop is None
                or self.loop.is_closed() or not self.isRunning()):
            self.response.emit(json.dumps({"id": request["id"], "ok": False,
                                           "error": {"code": "native_not_ready"}}))
            return
        coroutine = self.execute(request, firmware_approval=firmware_approval)
        try:
            asyncio.run_coroutine_threadsafe(coroutine, self.loop)
        except RuntimeError:
            coroutine.close()
            self.response.emit(json.dumps({"id": request["id"], "ok": False,
                                           "error": {"code": "native_not_ready"}}))

    async def execute(self, request, *, firmware_approval=None):
        task = asyncio.current_task()
        self.jobs.add(task)
        if firmware_approval is not None:
            self.firmware_launches.add(task)
        try:
            if len(self.jobs) > 8:
                raise RuntimeError("busy")
            if firmware_approval is not None:
                # Startup can launch a detached worker. Never cancel that launch
                # at a generic request deadline while its ownership is ambiguous.
                result = await self.transport.start_firmware(request, firmware_approval)
            else:
                result = await asyncio.wait_for(self.transport.dispatch(request), timeout=30)
            answer = {"id": request["id"], "ok": True, "result": result}
        except asyncio.CancelledError:
            answer = {"id": request["id"], "ok": False, "error": {"code": "cancelled"}}
        except Exception as error:
            # No exception text, stack, private GATT bytes or local paths cross.
            code = getattr(error, "code", "native_operation_failed")
            if not isinstance(code, str) or not re.fullmatch(r"[a-z_]{1,64}", code):
                code = "native_operation_failed"
            answer = {"id": request["id"], "ok": False, "error": {"code": code}}
        finally:
            self.jobs.discard(task)
            self.firmware_launches.discard(task)
        self.response.emit(json.dumps(answer))

    def stop(self):
        if self.closing:
            return
        self.closing = True
        if self.loop and self.ready.is_set():
            asyncio.run_coroutine_threadsafe(self.shutdown(), self.loop)

    async def shutdown(self):
        if self._shutdown_started:
            return
        self._shutdown_started = True
        # A started installer is independent of request timeouts. Let startup
        # settle, then wait for its writer before disconnecting other transports.
        for task in list(self.jobs - self.firmware_launches):
            task.cancel()
        if self.jobs:
            await asyncio.gather(*list(self.jobs), return_exceptions=True)
        wait_firmware = getattr(self.transport, 'wait_firmware', None)
        if wait_firmware is not None:
            await wait_firmware()
        try:
            await asyncio.wait_for(self.transport.dispatch({"op": "disconnect"}), timeout=8)
        except Exception:
            self.emit_event({"type": "cleanup_failed"})
        finally:
            self.loop.stop()


class Bridge(QObject):
    response = Signal(str)
    events = Signal(str)

    def __init__(self, worker, window):
        super().__init__(window)
        self.worker = worker
        self.window = window
        self.last_id = 0
        self.request_count = 0
        worker.response.connect(self.response)
        worker.event.connect(self.events)

    @Slot(str)
    def request(self, text):
        self.request_count += 1
        identifier = 0
        try:
            if not isinstance(text, str) or len(text.encode("utf-8")) > 4096:
                raise ValueError()
            data = json.loads(text)
            if not isinstance(data, dict):
                raise ValueError()
            identifier = data.get("id")
            if type(identifier) is not int or not self.last_id < identifier <= 0x7fffffff:
                raise ValueError()
            self.last_id = identifier
            op = data.get("op")
            if op == "app_info" and set(data) == {"id", "op"}:
                self.reply(identifier, {"version": __version__, "settings_transport": "native_bluetooth",
                                        "usb_settings": False, "firmware_install": True})
            elif op == 'startup_status' and set(data) == {'id', 'op'}:
                from . import startup
                self.reply(identifier, {'enabled': startup.enabled(), 'supported': bool(getattr(sys, 'frozen', False))})
            elif op == 'startup_toggle' and set(data) == {'id', 'op'}:
                from . import startup
                value = not startup.enabled()
                accepted = QMessageBox.question(self.window, 'Start with my computer?',
                    'Enable a small watcher that notices the stick and opens Setup after sign-in? It never installs firmware or opens a Bluetooth connection on its own.' if value else 'Turn off the Cloud Stick startup watcher?',
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
                if accepted == QMessageBox.StandardButton.Yes:
                    startup.configure(value)
                    if value:
                        import subprocess
                        from .app_install import clean_environment
                        subprocess.Popen([startup.executable(), '--watch'], env=clean_environment(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                self.reply(identifier, {'enabled': startup.enabled(), 'supported': True})
            elif op in {'app_update_state' , 'app_update_check', 'app_update_download'}:
                self.worker.submit(data)
            elif op == 'app_update_install' and set(data) == {'id', 'op'}:
                transport = self.worker.transport
                transport._firmware_idle_transport()
                if transport._firmware is not None and transport._firmware.blocked:
                    raise ValueError()
                path, release = transport._app_updates.installation()
                approved_release = dict(release)
                accepted = QMessageBox.question(self.window, 'Update Cloud Stick Setup?',
                    'Install Cloud Stick Setup ' + release['version'] + '?\n\n'
                    'Setup will close. Your settings and recovery copies stay on this computer. '
                    'The operating system may ask you to confirm installation.',
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
                if accepted != QMessageBox.StandardButton.Yes:
                    self.reply(identifier, {'cancelled': True})
                else:
                    transport._firmware_idle_transport()
                    if transport._firmware is not None and transport._firmware.blocked:
                        raise ValueError()
                    path, release = transport._app_updates.installation()
                    if release != approved_release:
                        raise ValueError()
                    from . import startup
                    startup.pause_watcher()
                    if release['platform'] == 'linux-user':
                        from .app_install import prepare_user_update
                        prepare_user_update(path, release)
                    elif release['platform'] == 'windows':
                        import subprocess
                        from .app_install import clean_environment
                        subprocess.Popen([str(path), '/CURRENTUSER'], env=clean_environment())
                    elif not QDesktopServices.openUrl(QUrl.fromLocalFile(str(path))):
                        raise ValueError()
                    self.reply(identifier, {'installing': True})
                    QTimer.singleShot(0, self.window.close)
            elif op == 'firmware_start':
                from .protocol import validate_request
                validate_request(data)
                manager = self.worker.transport.firmware
                token, target = manager.offer(data)
                action = {'new': 'Install on a NEW original LILYGO T-Dongle S3',
                          'existing': 'Update this existing Cloud Stick',
                          'resume': 'Resume the interrupted installation'}[data['mode']]
                text = (action + '?\n\nFirmware: ' + target + '\nUSB port: ' + data['port'] +
                        '\n\nConfirm this is the ORIGINAL 16 MB T-Dongle S3. Close file copies, safely eject any USB drive shown for it, unplug it, '
                        'then hold BOOT while plugging it back in and release BOOT. '
                        'Remove the microSD before installing a new board.\n\n'
                        'The app first makes and checks two private encrypted recovery copies. '
                        'Keep the computer powered and the USB connected through verification. '
                        'Existing Cloud Stick settings and backup records are preserved. '
                        'A new-board installation replaces reviewed manufacturer firmware.')
                accepted = QMessageBox.question(self.window, 'Install reviewed firmware?', text,
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No, QMessageBox.StandardButton.No)
                if accepted != QMessageBox.StandardButton.Yes:
                    manager._approvals.pop(token, None)
                    self.reply(identifier, {'cancelled': True})
                else:
                    self.worker.submit(data, firmware_approval=token)
            elif op == 'firmware_import' and set(data) == {'id', 'op'}:
                transport = self.worker.transport
                transport._firmware_idle_transport()
                catalog_path, _ = QFileDialog.getOpenFileName(self.window, 'Choose the signed firmware catalog', '', 'Signed catalog (*.json)')
                bundle_path = ''
                if catalog_path:
                    bundle_path, _ = QFileDialog.getOpenFileName(self.window, 'Choose its firmware package', '', 'Cloud Stick firmware (*.cspfw)')
                transport._firmware_idle_transport()
                self.reply(identifier, transport.firmware.import_offline(catalog_path, bundle_path) if bundle_path else {'cancelled': True})
            elif op in {'firmware_check', 'firmware_ports', 'firmware_prepare', 'firmware_state'}:
                self.worker.submit(data)
            elif op == "review_bundle" and set(data) == {"id", "op"}:
                path, _ = QFileDialog.getOpenFileName(self.window, "Review a release manifest", "", "Release manifest (*.json)")
                self.reply(identifier, review_manifest(path) if path else {"cancelled": True})
            elif op in {"discover", "connect", "read", "write", "disconnect", "usb_ports", "usb_watch_ports", "usb_status", "usb_open_setup",
                        "usb_setup_access_status", "usb_setup_access", "usb_forget_setup_access",
                        "usb_owner_status", "usb_owner_identity", "usb_owner_install", "usb_diagnostics", "usb_address", "lan_discover", "lan_status"}:
                self.worker.submit(data)
            else:
                raise ValueError()
        except Exception:
            self.response.emit(json.dumps({"id": identifier if type(identifier) is int else 0,
                                           "ok": False, "error": {"code": "invalid_request"}}))

    def reply(self, identifier, result):
        self.response.emit(json.dumps({"id": identifier, "ok": True, "result": result}))


class Window(QMainWindow):
    def __init__(self, factory=None):
        super().__init__()
        self.setWindowTitle("Cloud Stick Setup")
        self.resize(1120, 880)
        self.worker = Worker(factory, self)
        self.worker.start()
        self.profile = QWebEngineProfile(self)  # Off-the-record; no disk login/cache.
        self.profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.MemoryHttpCache)
        self.profile.setPersistentCookiesPolicy(QWebEngineProfile.PersistentCookiesPolicy.NoPersistentCookies)
        self.assets = LocalAssets(frontend_root(), self.profile)
        self.profile.installUrlSchemeHandler(b"cloudstick", self.assets)
        self.guard = LocalOnly(self.profile)
        self.profile.setUrlRequestInterceptor(self.guard)
        self.view_widget = QWebEngineView(self)
        self.page = LocalPage(self.profile, self.view_widget)
        self.page.synthetic_console = [] if factory is SmokeTransport else None
        self.view_widget.setPage(self.page)
        self.channel = QWebChannel(self.page)
        self.bridge = Bridge(self.worker, self)
        self.channel.registerObject("desktop", self.bridge)
        self.page.setWebChannel(self.channel)
        settings = self.page.settings()
        for flag in (QWebEngineSettings.WebAttribute.LocalContentCanAccessRemoteUrls,
                     QWebEngineSettings.WebAttribute.LocalContentCanAccessFileUrls,
                     QWebEngineSettings.WebAttribute.JavascriptCanOpenWindows):
            settings.setAttribute(flag, False)
        self.setCentralWidget(self.view_widget)
        self.page.setUrl(QUrl("cloudstick://app/index.html"))
        self.exit_ready = False
        self.worker.finished.connect(self.worker_closed)

    def closeEvent(self, event):
        if self.exit_ready or not self.worker.isRunning():
            event.accept()
            return
        event.ignore()
        self.statusBar().showMessage("Finishing any firmware work and closing the connection safely. Keep the USB connected…")
        self.worker.stop()

    def worker_closed(self):
        self.exit_ready = True
        self.close()


class SmokeTransport:
    """No native libraries/devices are touched in smoke mode."""

    def __init__(self, event_sink):
        self.event_sink = event_sink

    async def dispatch(self, request):
        if request["op"] in ("usb_ports", "usb_watch_ports"):
            return []
        if request['op'] == 'firmware_state':
            return {'phase':'idle','done':0,'total':1,'code':'','online':False,'version':'0.4.6-cache-dev'}
        if request['op'] in ('app_update_check','app_update_state'):
            return {'phase':'current','current':__version__,'version':'0.6.0','done':0,'total':1}
        if request["op"] == "discover":
            return {"devices": []}
        if request["op"] == "disconnect":
            return {}
        raise ValueError("synthetic_only")


def disable_core_dumps():
    """Keep local authentication material out of POSIX crash dumps."""
    if sys.platform != "win32":
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    disable_core_dumps()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--smoke-test", action="store_true")
    parser.add_argument("--smoke-panel", choices=("home", "firmware"), default="home")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--screenshot", type=Path)
    args = parser.parse_args()
    instance_lock = None
    if not args.smoke_test:
        from .firmware_platform import data_root
        from .firmware_recovery import private_directory
        root = data_root()
        private_directory(root)
        instance_lock = QLockFile(str(root / 'desktop-app.lock'))
        instance_lock.setStaleLockTime(0)
        if not instance_lock.tryLock(0):
            return 0
    register_scheme()
    app = QApplication([sys.argv[0]])
    app.setApplicationName("Cloud Stick Setup")
    app.setApplicationVersion(__version__)
    app.setOrganizationName("Cloud Stick")
    if not args.smoke_test:
        from .startup import resume_watcher
        resume_watcher()
    window = Window(SmokeTransport if args.smoke_test else None)
    window.show()
    if args.smoke_test:
        checks = {"mode": "synthetic_desktop_smoke", "hardware_contact": False, "passed": False}
        checks["native_responses"] = []
        window.bridge.response.connect(lambda value: checks["native_responses"].append(reportable_response(value)))

        def collect():
            script = """JSON.stringify({title:document.title,secure:globalThis.isSecureContext,
              crypto:!!globalThis.crypto?.subtle,login:!!document.getElementById('login-username'),
              connectEnabled:!document.getElementById('connect')?.disabled,
              nativeReady:document.getElementById('desktop-status-message')?.textContent.startsWith('No supported USB'),
              nativeStatus:document.getElementById('desktop-status-message')?.textContent,
              cryptoRoundTrip:globalThis.__smokeCrypto===true,
              cryptoError:globalThis.__smokeCryptoError??'',
              bodyText:document.body.innerText.slice(0,180)})"""

            def received(value):
                try:
                    checks.update(json.loads(value))
                    checks["passed"] = all(checks.get(k) is True for k in ("secure", "crypto", "login", "connectEnabled", "nativeReady", "cryptoRoundTrip"))
                except Exception:
                    checks["passed"] = False
                if args.screenshot:
                    args.screenshot.parent.mkdir(parents=True, exist_ok=True)
                    window.grab().save(str(args.screenshot))
                if args.report:
                    checks["request_count"] = window.bridge.request_count
                    checks["console"] = window.page.synthetic_console
                    checks["off_the_record"] = window.profile.isOffTheRecord()
                    args.report.parent.mkdir(parents=True, exist_ok=True)
                    args.report.write_text(json.dumps(checks, indent=2) + "\n")
                print(json.dumps(checks), flush=True)
                window.close()

            window.page.runJavaScript(script, received)

        def exercise():
            window.page.runJavaScript("""document.getElementById('desktop-refresh')?.click();
              import('cloudstick://app/shared/security2.mjs').then(async ({Security2Client})=>{
                const client=new Security2Client('synthetic-owner','synthetic-password-for-tests');
                const packet=await client.start(); client.destroy();
                const key=await crypto.subtle.generateKey({name:'AES-GCM',length:256},false,['encrypt','decrypt']);
                const iv=crypto.getRandomValues(new Uint8Array(12)), plain=new Uint8Array([1,2,3]);
                const cipher=await crypto.subtle.encrypt({name:'AES-GCM',iv},key,plain);
                const out=new Uint8Array(await crypto.subtle.decrypt({name:'AES-GCM',iv},key,cipher));
                globalThis.__smokeCrypto=packet.length>384&&packet.length<=512&&out.join(',')==='1,2,3';
              }).catch(error=>{globalThis.__smokeCrypto=false;globalThis.__smokeCryptoError=error.name+': '+error.message;});""")

        if args.smoke_panel == 'firmware':
            QTimer.singleShot(3500, lambda: window.page.runJavaScript("document.getElementById('firmware-new-stick')?.click()"))
        QTimer.singleShot(4000, exercise)
        QTimer.singleShot(8000, collect)
        QTimer.singleShot(22000, window.close)
    code = app.exec()
    # The view/page must die before its off-record profile. Qt's QObject child
    # creation order otherwise destroys this profile first at process teardown.
    from shiboken6 import delete, isValid
    if isValid(window.page):
        delete(window.page)
    if isValid(window.profile):
        delete(window.profile)
    if args.smoke_test and not checks["passed"]:
        return 1
    return code


if __name__ == "__main__":
    sys.exit(main())
