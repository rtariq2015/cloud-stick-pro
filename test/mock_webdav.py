"""Loopback-only synthetic fixture; never connects to a real cloud account."""
import base64
import hashlib
import http.server
import subprocess
import tempfile
import threading
import urllib.parse
from pathlib import Path

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    seen = set()
    def log_message(self, *_):
        pass
    def reply(self, status, body=b''):
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def key(self):
        return urllib.parse.unquote(self.path).lstrip('/')
    def do_PUT(self):
        key = self.key()
        length = int(self.headers.get('Content-Length', '0'))
        first = key not in self.seen
        self.seen.add(key)
        interrupt = first and key.startswith('disconnect')
        # Bounded streaming fixture; large files aren't held in a Python bytes object.
        dest = self.server.root / hashlib.sha256(key.encode()).hexdigest()
        with dest.open('wb') as f:
            remaining = max(1, length // 2) if interrupt else length
            while remaining:
                block = self.rfile.read(min(2048, remaining))
                if not block:
                    return
                f.write(block)
                remaining -= len(block)
        if interrupt:
            self.close_connection = True
            self.connection.shutdown(2)
            return
        if self.headers.get('Authorization') != 'Basic ' + base64.b64encode(b'mock:pass').decode():
            return self.reply(401)
        if first and key == 'auth.txt':
            return self.reply(401)
        if first and key == 'down.txt':
            return self.reply(503)
        if first and key == 'corrupt.txt':
            dest.write_bytes(b'wrong content')
        self.reply(201)
    def do_GET(self):
        dest = self.server.root / hashlib.sha256(self.key().encode()).hexdigest()
        if not dest.exists():
            return self.reply(404)
        self.send_response(200)
        self.send_header('Content-Length', str(dest.stat().st_size))
        self.end_headers()
        with dest.open('rb') as f:
            while block := f.read(2048):
                self.wfile.write(block)

if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='csp-mock-') as root:
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        server.root = Path(root)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = subprocess.run(['artifacts/native-tests', f'http://127.0.0.1:{server.server_port}/'])
        finally:
            server.shutdown()
            server.server_close()
        raise SystemExit(result.returncode)
