"""Local-only fixture preview. Serve exactly four public static files."""
from argparse import ArgumentParser
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent
ALLOWED = {'/', '/index.html', '/style.css', '/app.mjs', '/preview-service.mjs'}
CSP = ("default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'none'; "
       "img-src 'none'; font-src 'none'; object-src 'none'; base-uri 'none'; "
       "form-action 'none'; frame-ancestors 'none'")
class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *_): pass
    def list_directory(self, path):
        # Even if index.html is absent, never fall back to listing test/source files.
        self.send_error(404)
        return None
    def do_GET(self):
        if urlsplit(self.path).path not in ALLOWED:
            self.send_error(404)
            return
        super().do_GET()
    def do_HEAD(self):
        if urlsplit(self.path).path not in ALLOWED:
            self.send_error(404)
            return
        super().do_HEAD()
    def do_POST(self): self.send_error(405)
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Security-Policy', CSP)
        self.send_header('Permissions-Policy', 'bluetooth=(), camera=(), microphone=()')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Referrer-Policy', 'no-referrer')
        super().end_headers()
    def guess_type(self, path):
        if path.endswith('.mjs'): return 'text/javascript'
        return super().guess_type(path)

if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8787)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.bind, args.port), partial(Handler, directory=str(ROOT)))
    print(f'Example-only Wi-Fi setup preview: http://{args.bind}:{server.server_port}/', flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close()
