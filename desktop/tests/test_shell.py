"""Qt-core/source boundaries only: no WebEngine view, device or real transport."""
import asyncio
import inspect
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest import mock

from PySide6.QtCore import QCoreApplication, QObject, QUrl, Signal
from cloudstick_desktop.main import (Bridge, LocalAssets, LocalOnly, LocalPage,
                                     Worker, Window, QWebEngineUrlRequestJob)
from cloudstick_desktop.release_review import review_manifest

APP = QCoreApplication.instance() or QCoreApplication([])


class FakeJob(QObject):
    def __init__(self, url, method=b"GET"):
        super().__init__()
        self.url = QUrl(url)
        self.method = method
        self.failure = self.body = self.mime = None

    def requestUrl(self):
        return self.url

    def requestMethod(self):
        return self.method

    def fail(self, failure):
        self.failure = failure

    def reply(self, mime, device):
        self.mime = bytes(mime)
        self.body = bytes(device.readAll())


class FakeWorker(QObject):
    response = Signal(str)
    event = Signal(str)

    def __init__(self):
        super().__init__()
        self.requests = []

    def submit(self, value):
        self.requests.append(value)


class ShellBoundaryTests(unittest.TestCase):
    def test_local_resource_handler_serves_only_existing_allowlisted_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "index.html").write_bytes(b"SYNTHETIC HTML")
            (root / "connection.mjs").write_bytes(b"// synthetic")
            (root / "private.json").write_text('{"password":"SYNTHETIC"}')
            handler = LocalAssets(root)
            for url, method in [("cloudstick://other/index.html", b"GET"),
                                ("cloudstick://app/private.json", b"GET"),
                                ("cloudstick://app/unknown.mjs", b"GET"),
                                ("cloudstick://app/index.html?x=1", b"GET"),
                                ("cloudstick://app/index.html#fragment", b"GET"),
                                ("cloudstick://app/index.html", b"POST")]:
                job = FakeJob(url, method)
                handler.requestStarted(job)
                self.assertEqual(job.failure, QWebEngineUrlRequestJob.Error.RequestDenied)
                self.assertIsNone(job.body)
            job = FakeJob("cloudstick://app/connection.mjs")
            handler.requestStarted(job)
            self.assertEqual(job.mime, b"text/javascript")
            self.assertEqual(job.body, b"// synthetic")

    def test_local_asset_symlink_cannot_read_outside_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "frontend"
            root.mkdir()
            outside = Path(directory) / "outside.mjs"
            outside.write_bytes(b"// synthetic outside")
            try:
                (root / "escape.mjs").symlink_to(outside)
            except OSError:
                self.skipTest("Test filesystem cannot create symbolic links")
            job = FakeJob("cloudstick://app/escape.mjs")
            LocalAssets(root).requestStarted(job)
            self.assertEqual(job.failure, QWebEngineUrlRequestJob.Error.RequestDenied)
            self.assertIsNone(job.body)

    def test_request_interceptor_denies_network_file_and_other_origins(self):
        class Request:
            def __init__(self, value):
                self.url, self.blocked = QUrl(value), None

            def requestUrl(self):
                return self.url

            def block(self, value):
                self.blocked = value

        guard = LocalOnly()
        for url in ["https://example.invalid/", "file:///tmp/synthetic",
                    "http://127.0.0.1/", "cloudstick://other/index.html",
                    "data:text/html,synthetic"]:
            r = Request(url)
            guard.interceptRequest(r)
            self.assertTrue(r.blocked)
        r = Request("cloudstick://app/index.html")
        guard.interceptRequest(r)
        self.assertFalse(r.blocked)

    def test_qt_page_external_parent_uses_existing_api(self):
        # PySide6 6.10 removed QWebEnginePage.view(). Keep the override compatible.
        source = inspect.getsource(LocalPage.acceptNavigationRequest)
        self.assertNotIn("self.view()", source)
        self.assertIn("self.parent()", source)

    def test_private_profile_configuration_is_explicit_not_a_named_disk_profile(self):
        # Actual off-record profile/WebCrypto is covered separately by Qt GUI smoke.
        source = inspect.getsource(Window.__init__)
        self.assertIn("QWebEngineProfile(self)", source)
        self.assertIn("MemoryHttpCache", source)
        self.assertIn("NoPersistentCookies", source)
        self.assertIn("LocalContentCanAccessRemoteUrls", source)
        self.assertIn("LocalContentCanAccessFileUrls", source)

    def test_native_request_ids_and_unknown_methods_fail_closed(self):
        worker, window = FakeWorker(), QObject()
        bridge = Bridge(worker, window)
        replies = []
        bridge.response.connect(lambda text: replies.append(json.loads(text)))
        bridge.request(json.dumps({"id": 1, "op": "usb_ports"}))
        self.assertEqual(worker.requests, [{"id": 1, "op": "usb_ports"}])
        for value in [{"id": 1, "op": "usb_ports"}, {"id": True, "op": "discover"},
                      {"id": 0x80000000, "op": "discover"}, {"id": 2, "op": "exec"},
                      {"id": 3, "op": "review_bundle", "path": "synthetic.json"}]:
            bridge.request(json.dumps(value))
            self.assertFalse(replies[-1]["ok"])
            self.assertEqual(replies[-1]["error"], {"code": "invalid_request"})
        self.assertEqual(len(worker.requests), 1)

    def test_exception_payloads_are_not_serialized(self):
        class Transport:
            async def dispatch(self, request):
                e = RuntimeError("SYNTHETIC-SECRET-MUST-NOT-LEAVE")
                e.code = "not-a-valid-code with secret"
                raise e

        worker = Worker(lambda _: Transport())
        worker.transport = Transport()
        replies = []
        worker.response.connect(replies.append)
        asyncio.run(worker.execute({"id": 1, "op": "usb_ports"}))
        self.assertNotIn("SECRET", replies[0])
        self.assertEqual(json.loads(replies[0])["error"]["code"], "native_operation_failed")

    def test_close_during_factory_initialization_finishes_and_disconnects_once(self):
        entered, release = threading.Event(), threading.Event()
        calls = []

        class Transport:
            async def dispatch(self, request):
                calls.append(request["op"])
                return {}

        def factory(_):
            entered.set()
            if not release.wait(2):
                raise RuntimeError("synthetic startup timeout")
            return Transport()

        worker = Worker(factory)
        worker.start()
        self.assertTrue(entered.wait(2))
        worker.stop()
        release.set()
        self.assertTrue(worker.wait(3000), "Closing during startup left the worker alive")
        self.assertEqual(calls, ["disconnect"])
        self.assertTrue(worker.loop.is_closed())

    def test_stop_exactly_at_ready_handoff_does_not_duplicate_shutdown(self):
        calls = []

        class Transport:
            async def dispatch(self, request):
                calls.append(request["op"])
                # Yield so overlapping shutdown coroutines are observable.
                await asyncio.sleep(.005)
                return {}

        worker = Worker(lambda _: Transport())

        class StopAtReady:
            def __init__(self):
                self.event, self.once = threading.Event(), False

            def is_set(self):
                return self.event.is_set()

            def set(self):
                self.event.set()
                if not self.once:
                    self.once = True
                    worker.stop()

        worker.ready = StopAtReady()
        worker.start()
        self.assertTrue(worker.wait(3000))
        self.assertEqual(calls, ["disconnect"])

    def test_factory_failure_closes_loop_without_private_exception_text(self):
        events = []

        def factory(_):
            raise RuntimeError("SYNTHETIC-SECRET")

        worker = Worker(factory)
        worker.event.connect(events.append)
        worker.start()
        self.assertTrue(worker.wait(3000))
        APP.processEvents()
        self.assertTrue(worker.loop.is_closed())
        self.assertEqual([json.loads(e) for e in events], [{"type": "startup_failed"}])

    def test_release_read_bound_is_enforced_on_opened_bytes_not_only_stat(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "release.json"
            path.write_text(json.dumps({"version": "synthetic", "ignored": "x" * (1024 * 1024)}))
            metadata = list(os.stat(path))
            metadata[6] = 1  # Simulate growth/change after an earlier size check.
            with mock.patch.object(Path, "stat", return_value=os.stat_result(metadata)):
                with self.assertRaises(ValueError):
                    review_manifest(path)


if __name__ == "__main__":
    unittest.main()
