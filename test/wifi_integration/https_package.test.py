"""Loopback MIME/header/allowlist fixture; not final-origin HTTPS acceptance."""
from pathlib import Path
import hashlib
import http.server
import importlib.util
import json
import re
import threading
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('phone_package', ROOT / 'tools/prepare_wifi_phone_package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
output = package.prepare()
manifest = json.loads((output / 'release.json').read_text())

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        name = 'index.html' if self.path == '/' else self.path[1:]
        if name not in package.FILES:
            self.send_error(404)
            return
        body = (output / 'public' / name).read_bytes()
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
        for name in ['.git/config', 'release.json', 'headers.json', 'firmware.bin',
                     'owner-card.json', '../src/main.cpp', 'index.html?file=owner-card.json']:
            try:
                urllib.request.urlopen(origin + '/' + name)
                raise AssertionError('Unexpected file served')
            except urllib.error.HTTPError as error:
                assert error.code == 404
    finally:
        server.shutdown()
        worker.join(timeout=3)
        assert not worker.is_alive()

security_source = (ROOT / 'phone/wifi-setup/security2.mjs').read_text()
helper = r'// BEGIN OWNER LOGIN RECORD V1\n[\s\S]*?// END OWNER LOGIN RECORD V1\n\n'
assert len(re.findall(helper, security_source)) == 1
baseline = re.sub(helper, '', security_source)
assert baseline.encode() == (ROOT / 'phone/milestone1/security2.mjs').read_bytes()
assert hashlib.sha256(baseline.encode()).hexdigest() == '83c66d50af453477f5bdba1cf70faada5332b9d16f9575afbd6a7436eda6a6ff'
(output / 'checks.json').write_text(json.dumps(dict(passed=True, files=5,
    excluded_paths=7, loopback_only=True, tls_or_android_test=False,
    security2_handshake_cipher_unchanged=True,
    separate_owner_record_helper_blocks=1), indent=2) + '\n')
print('PASS five allowlisted files, hashes, exact JS MIME/security/cache headers; seven private paths denied (loopback only)')
