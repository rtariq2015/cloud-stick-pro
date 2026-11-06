"""Local five-file package mechanics; no publication or final-origin TLS test."""
import hashlib
import http.server
import importlib.util
import json
import threading
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('owner_login_phone_package', ROOT / 'tools/prepare_wifi_phone_package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
out = package.prepare()
manifest = json.loads((out / 'release.json').read_text())
assert manifest['deployed'] is False and manifest['device_or_private_key_included'] is False
assert set(path.name for path in (out / 'public').iterdir()) == set(package.FILES)

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        name = 'index.html' if self.path == '/' else self.path[1:]
        if name not in package.FILES:
            self.send_error(404)
            return
        body = (out / 'public' / name).read_bytes()
        self.send_response(200)
        for key, value in package.HEADERS.items():
            self.send_header(key, value)
        self.send_header('Content-Type', package.MIMES[name])
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    origin = 'http://127.0.0.1:' + str(server.server_port)
    try:
        for name in package.FILES:
            with urllib.request.urlopen(origin + '/' + name) as response:
                assert hashlib.sha256(response.read()).hexdigest() == manifest['public_files'][name]['sha256']
                assert response.headers['Content-Type'] == package.MIMES[name]
                for key, value in package.HEADERS.items():
                    assert response.headers[key] == value
        for name in ['owner-card.json', 'owner-record.bin', 'release.json', '.git/config', 'firmware.bin', '../src/main.cpp', 'index.html?password=PUBLIC_SYNTHETIC']:
            try:
                urllib.request.urlopen(origin + '/' + name)
                raise AssertionError('Private or unexpected resource was served')
            except urllib.error.HTTPError as error:
                assert error.code == 404
    finally:
        server.shutdown()
        worker.join(timeout=3)
        assert not worker.is_alive()

evidence = ROOT / 'artifacts/owner-login-phone'
evidence.mkdir(parents=True, exist_ok=True)
(evidence / 'package-checks.json').write_text(json.dumps(dict(passed=True, files=5,
    excluded_paths=7, loopback_only=True, deployed=False, final_https_origin_verified=False,
    manifest_retains_historical_label=True, handshake_provenance_test='test/owner_login/crypto.test.mjs'), indent=2) + '\n')
print('PASS owner-login five-file hashes/MIME/security/cache headers and 7 denied paths on loopback; no deployment')
