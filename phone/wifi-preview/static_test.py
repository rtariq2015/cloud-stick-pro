"""Test the actual allowlisted local server, with no browser or external traffic."""
from functools import partial
from http.server import ThreadingHTTPServer
from html.parser import HTMLParser
from pathlib import Path
from threading import Thread
from tempfile import TemporaryDirectory
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import hashlib
import json
from serve import Handler, ROOT, CSP

class Inputs(HTMLParser):
    def __init__(self): super().__init__(); self.fields = []
    def handle_starttag(self, tag, attrs):
        if tag == 'input': self.fields.append(dict(attrs))

server = ThreadingHTTPServer(('127.0.0.1', 0), partial(Handler, directory=str(ROOT)))
thread = Thread(target=server.serve_forever, daemon=True)
thread.start()
url = f'http://127.0.0.1:{server.server_port}'
results = []
try:
    for name, mime in [('index.html', 'text/html'), ('style.css', 'text/css'),
                       ('app.mjs', 'text/javascript'), ('preview-service.mjs', 'text/javascript')]:
        with urlopen(url + '/' + name) as response:
            payload = response.read()
            assert payload == (ROOT / name).read_bytes()
            assert response.headers.get_content_type() == mime
            assert response.headers['Content-Security-Policy'] == CSP
            assert "connect-src 'none'" in CSP
            assert response.headers['Cache-Control'] == 'no-store'
            assert response.headers['Permissions-Policy'].startswith('bluetooth=()')
            assert response.headers['X-Content-Type-Options'] == 'nosniff'
            results.append({'file': name, 'mime': mime, 'sha256': hashlib.sha256(payload).hexdigest()})
        with urlopen(Request(url + '/' + name, method='HEAD')) as response:
            assert response.status == 200 and response.read() == b''
    for path in ['/serve.py', '/app.test.mjs', '/.git/config', '/../README.md',
                 '/%2e%2e/README.md', '/firmware.bin', '/NVS.bin', '/private/', '/style.css/']:
        for method in ['GET', 'HEAD']:
            try: urlopen(Request(url + path, method=method))
            except HTTPError as e: assert e.code == 404
            else: raise AssertionError(path + ' should not be served')
    try: urlopen(Request(url + '/', data=b'not-a-real-secret', method='POST'))
    except HTTPError as e: assert e.code == 405
    else: raise AssertionError('POST should be rejected')
    fields = Inputs(); fields.feed((ROOT / 'index.html').read_text())
    assert len(fields.fields) == 1 and fields.fields[0]['type'] == 'password'
    assert 'readonly' in fields.fields[0]
    for name in ['app.mjs', 'preview-service.mjs']:
        script = (ROOT / name).read_text()
        for forbidden in ['fetch(', 'XMLHttpRequest', 'WebSocket', 'navigator.bluetooth',
                          'localStorage', 'sessionStorage', 'innerHTML']:
            assert forbidden not in script, (name, forbidden)
    with TemporaryDirectory() as missing:
        listing = ThreadingHTTPServer(('127.0.0.1', 0), partial(Handler, directory=missing))
        worker = Thread(target=listing.serve_forever, daemon=True); worker.start()
        try:
            try: urlopen(f'http://127.0.0.1:{listing.server_port}/')
            except HTTPError as e: assert e.code == 404
            else: raise AssertionError('Missing index must never produce a directory listing')
        finally:
            listing.shutdown(); listing.server_close(); worker.join(timeout=5)
    out = ROOT.parents[1] / 'artifacts/wifi-candidate'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'static-preview-results.json').write_text(json.dumps({
        'passed': True, 'files': results, 'negative_routes': 18,
        'post_rejected': True, 'missing_index_listing_denied': True,
        'credential_entry': 'readonly fixed synthetic fixture only',
        'scope': 'actual loopback HTTP/static checks; no browser rendering or Bluetooth'
    }, indent=2) + '\n')
    print('PASS four static files, MIME/CSP/security headers, 18 denied routes, denied POST and no missing-index listing')
finally:
    server.shutdown(); server.server_close(); thread.join(timeout=5)
