"""Prepare an allowlisted HTTPS release for review. Never deploys or publishes."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
FILES = ('index.html', 'style.css', 'app.mjs', 'connection.mjs', 'security2.mjs')
MIMES = {'index.html': 'text/html; charset=utf-8', 'style.css': 'text/css; charset=utf-8',
         **{f: 'text/javascript; charset=utf-8' for f in FILES if f.endswith('.mjs')}}
CSP = ("default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'none'; "
       "img-src 'none'; font-src 'none'; object-src 'none'; base-uri 'none'; "
       "form-action 'none'; frame-ancestors 'none'")
HEADERS = {'Content-Security-Policy': CSP,
    'Permissions-Policy': 'bluetooth=(self), camera=(), microphone=(), geolocation=()',
    'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
    'Cache-Control': 'no-store', 'X-Frame-Options': 'DENY',
    'Strict-Transport-Security': 'max-age=31536000'}

def prepare():
    source = ROOT / 'phone/wifi-setup'
    output = ROOT / 'artifacts/wifi-integration/phone-https'
    public = output / 'public'
    public.mkdir(parents=True, exist_ok=True)
    assert not public.is_symlink()
    for p in public.iterdir():
        if p.name not in FILES or p.is_symlink() or not p.is_file():
            raise RuntimeError('Unexpected public package entry')
    records = {}
    for name in FILES:
        src = source / name
        assert src.is_file() and not src.is_symlink()
        dest = public / name
        shutil.copyfile(src, dest)
        records[name] = {'bytes': dest.stat().st_size,
            'sha256': hashlib.sha256(dest.read_bytes()).hexdigest(),
            'Content-Type': MIMES[name]}
    assert set(p.name for p in public.iterdir()) == set(FILES)
    manifest = {'release': '0.3.3-wifi-m2-dev-phone.1', 'protocol': 'csp-wifi v2 / Security2 patch1',
        'public_files': records, 'deployed': False,
        'device_or_private_key_included': False, 'final_https_origin_verified': False}
    (output / 'release.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'headers.json').write_text(json.dumps(dict(common=HEADERS, mime=MIMES), indent=2) + '\n')
    return output

if __name__ == '__main__':
    print('Prepared five-file review package:', prepare())
