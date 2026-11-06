#!/usr/bin/env python3
"""Prepare a reviewable static phone release; never installs or starts a server."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

REPO = Path(__file__).resolve().parents[1]
RUNTIME = ('index.html', 'style.css', 'app.mjs', 'connection.mjs', 'security2.mjs')
HEADERS = {
    'Content-Security-Policy': "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'none'; font-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-src 'none'; frame-ancestors 'none'; worker-src 'none'; manifest-src 'none'",
    'Permissions-Policy': 'bluetooth=(self), camera=(), microphone=(), geolocation=(), usb=(), serial=()',
    'X-Content-Type-Options': 'nosniff',
    'X-Frame-Options': 'DENY',
    'Referrer-Policy': 'no-referrer',
    'Cache-Control': 'no-store',
    'Strict-Transport-Security': 'max-age=31536000',
}

def nginx_string(value):
    value = str(value)
    if any(ord(c) < 32 for c in value) or '$' in value:
        raise ValueError('Unsafe nginx string')
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'

def nginx_config(package, release, certificate, key, port=8443):
    """Contained example. Loopback only until a host/origin is separately approved."""
    prefix = '/releases/' + release + '/'
    headers = '\n'.join('        add_header '+name+' '+nginx_string(value)+' always;' for name, value in HEADERS.items())
    routes = '\n'.join('        location = '+prefix+name+' { limit_except GET { deny all; } try_files $uri =404; }' for name in RUNTIME)
    return f'''# REVIEW EXAMPLE ONLY: no deployment/installation is performed.
# Keep immutable version paths. Prepare a new complete directory, verify hashes,
# then rename on the same filesystem and atomically reload the selected config.
# Retain old version directories; never replace an existing version's modules.
worker_processes 1;
pid {nginx_string(package / 'nginx.pid')};
error_log {nginx_string(package / 'nginx-error.log')} warn;
events {{ worker_connections 64; }}
http {{
    access_log off;
    server_tokens off;
    types {{ text/html html; text/css css; text/javascript mjs; }}
    default_type application/octet-stream;
    client_body_temp_path {nginx_string(package / 'tmp')};
    proxy_temp_path {nginx_string(package / 'tmp/proxy')};
    fastcgi_temp_path {nginx_string(package / 'tmp/fastcgi')};
    uwsgi_temp_path {nginx_string(package / 'tmp/uwsgi')};
    scgi_temp_path {nginx_string(package / 'tmp/scgi')};
    server {{
        listen 127.0.0.1:{port} ssl;
        server_name localhost;
        root {nginx_string(package / 'public')};
        ssl_certificate {nginx_string(certificate)};
        ssl_certificate_key {nginx_string(key)};
        ssl_protocols TLSv1.2 TLSv1.3;
        ssl_ciphers HIGH:!aNULL:!MD5:!3DES;
        ssl_session_tickets off;
        autoindex off;
        disable_symlinks on;
        etag off;
        if_modified_since off;
{headers}
        location = / {{ return 302 {prefix}index.html; }}
        location = {prefix} {{ return 302 {prefix}index.html; }}
{routes}
        location / {{ return 404; }}
    }}
}}
'''

def prepare(output, release, source=None, certificate=None, key=None, port=8443):
    if not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,63}', release):
        raise ValueError('Release must be a short lowercase version identifier')
    if not 1024 <= port <= 65535:
        raise ValueError('Use an unprivileged TCP port')
    source = Path(source or REPO / 'phone/milestone1').absolute()
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError('Refusing to replace an existing release package')
    if source.is_symlink():
        raise ValueError('Source directory cannot be a symlink')
    data = {}
    for name in RUNTIME:
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Runtime file must be a regular, non-symlink file: ' + name)
        data[name] = path.read_bytes()
    certificate = Path(certificate or output / 'REVIEWED-fullchain.pem').absolute()
    key = Path(key or output / 'REVIEWED-private-key.pem').absolute()
    config = nginx_config(output, release, certificate, key, port)
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.' + output.name + '-', dir=output.parent))
    try:
        public = staging / 'public/releases' / release
        public.mkdir(parents=True)
        for name, content in data.items():
            (public / name).write_bytes(content)
            (public / name).chmod(0o644)
        files = {name: {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()} for name, content in data.items()}
        manifest = {'schema': 1, 'release': release, 'url_path': '/releases/' + release + '/index.html', 'scope': 'local staging only; synthetic-only client; no deployment or device contact', 'files': files, 'headers': HEADERS, 'config_sha256': hashlib.sha256(config.encode()).hexdigest(), 'public_file_count': 5, 'cache_policy': 'no-store; immutable version paths; no service worker', 'certificate_policy': 'Trusted owner-approved origin/chain required before publishing; generated config binds only loopback. Test CA is not a phone deployment certificate.'}
        (staging / 'release-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (staging / 'nginx.conf').write_text(config)
        (staging / 'tmp').mkdir()
        # Attribution is also embedded in security2.mjs, so the public allowlist
        # remains exactly five runtime files. Keep provenance for reviewers here.
        for name in ('PROVENANCE.md', 'vendor/ESPRESSIF-LICENSE'):
            path = source / name
            if path.is_file() and not path.is_symlink():
                (staging / Path(name).name).write_bytes(path.read_bytes())
        if {p.name for p in public.iterdir()} != set(RUNTIME):
            raise ValueError('Unexpected public runtime contents')
        os.rename(staging, output)
    except BaseException:
        shutil.rmtree(staging)
        raise
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--release', required=True)
    parser.add_argument('--certificate', type=Path)
    parser.add_argument('--key', type=Path)
    parser.add_argument('--port', type=int, default=8443)
    args = parser.parse_args()
    manifest = prepare(args.output, args.release, certificate=args.certificate, key=args.key, port=args.port)
    print(json.dumps({'package': str(args.output.absolute()), 'release': manifest['release'], 'runtime_files': list(manifest['files']), 'deployed': False}))

if __name__ == '__main__':
    main()
