#!/usr/bin/env python3
"""Local-only NGINX/TLS/browser acceptance. No physical BLE or public deployment."""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import time
import unittest
from urllib.parse import urlsplit
from phone_https_package import HEADERS, REPO, RUNTIME, prepare

def run(*args, **kwargs):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, **kwargs)
    if result.returncode:
        raise RuntimeError('Local test command failed: ' + str(args[0]) + '\n' + result.stderr)
    return result

class PackageTests(unittest.TestCase):
    def test_exact_files_and_atomic_nonreplacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            shutil.copytree(REPO / 'phone/milestone1', source)
            (source / '.private-backup').write_text('synthetic unserved canary')
            output = root / 'package'
            manifest = prepare(output, 'm1-test', source=source)
            actual = [p for p in (output / 'public').rglob('*') if p.is_file()]
            self.assertEqual(sorted(p.name for p in actual), sorted(RUNTIME))
            self.assertFalse((output / 'public/release-manifest.json').exists())
            self.assertEqual(manifest['public_file_count'], 5)
            with self.assertRaises(FileExistsError):
                prepare(output, 'm1-test', source=source)
            for bad in ('../private', 'a/b', 'A', '', 'x\nlocation'):
                with self.assertRaises(ValueError):
                    prepare(root / 'new', bad, source=source)
            (source / 'app.mjs').unlink()
            (source / 'app.mjs').symlink_to(source / 'connection.mjs')
            with self.assertRaises(ValueError):
                prepare(root / 'new', 'm1-test', source=source)
            self.assertFalse((root / 'new').exists())

def generate_certificates(root):
    run('openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-keyout', root / 'ca.key', '-out', root / 'ca.pem', '-subj', '/CN=Cloud Stick ephemeral localhost test CA', '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign')
    run('openssl', 'req', '-new', '-newkey', 'rsa:2048', '-nodes', '-keyout', root / 'server.key', '-out', root / 'server.csr', '-subj', '/CN=localhost')
    (root / 'server.ext').write_text('subjectAltName=DNS:localhost,IP:127.0.0.1\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n')
    run('openssl', 'x509', '-req', '-in', root / 'server.csr', '-CA', root / 'ca.pem', '-CAkey', root / 'ca.key', '-CAcreateserial', '-out', root / 'server.pem', '-days', '1', '-extfile', root / 'server.ext')
    for name in ('ca.key', 'server.key'):
        (root / name).chmod(0o600)

def integration(output, browser=False):
    with tempfile.TemporaryDirectory(prefix='csp-local-https-') as temporary:
        root = Path(temporary)
        generate_certificates(root)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        package = root / 'package'
        manifest = prepare(package, 'm1-test', certificate=root / 'server.pem', key=root / 'server.key', port=port)
        run('nginx', '-t', '-c', package / 'nginx.conf', '-p', package)
        process = subprocess.Popen(['nginx', '-c', str(package / 'nginx.conf'), '-p', str(package), '-g', 'daemon off; master_process off;'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            context = ssl.create_default_context(cafile=str(root / 'ca.pem'))
            for attempt in range(100):
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.1):
                        break
                except OSError:
                    time.sleep(.02)
            else:
                raise AssertionError('Contained NGINX did not start')
            def request(path, method='GET', tls_context=context, request_headers=None):
                connection = http.client.HTTPSConnection('localhost', port, context=tls_context, timeout=5)
                try:
                    connection.request(method, path, headers=request_headers or {})
                    response = connection.getresponse()
                    headers = dict(response.getheaders())
                    content = response.read()
                    for name, value in HEADERS.items():
                        assert headers.get(name) == value, (path, name, headers)
                    assert 'Access-Control-Allow-Origin' not in headers
                    return response.status, headers, content
                finally:
                    connection.close()
            prefix = '/releases/m1-test/'
            checks = []
            for name in RUNTIME:
                status, headers, content = request(prefix + name)
                assert status == 200
                assert hashlib.sha256(content).hexdigest() == manifest['files'][name]['sha256']
                expected = 'text/javascript' if name.endswith('.mjs') else 'text/css' if name.endswith('.css') else 'text/html'
                assert headers['Content-Type'] == expected, headers
                assert request(prefix + name, 'HEAD')[0] == 200
                checks.append({'file': name, 'status': status, 'mime': expected, 'hash_verified': True})
            assert request('/')[0] == 302
            assert request(prefix)[0] == 302
            denied = ['/.git/config', '/test/', '/test/gatt_mock.js', prefix + 'test/gatt_mock.js', '/artifacts/', '/backups/', '/private/', '/release-manifest.json', '/nginx.conf', prefix + 'index.html.bak', '/releases/', '/README.md', '/vendor/ESPRESSIF-LICENSE']
            # An accidental extra file is denied by exact routes too; remove it
            # before verifying that the finished public tree contains five files.
            canary = package / 'public/releases/m1-test/private-backup.bin'
            canary.write_text('SYNTHETIC-UNSERVED-CANARY')
            denied.append(prefix + canary.name)
            for path in denied:
                status, _, content = request(path)
                assert status == 404, (path, status)
                assert b'SYNTHETIC-UNSERVED-CANARY' not in content
                assert b'Index of' not in content
            canary.unlink()
            assert request(prefix + 'app.mjs', 'POST')[0] == 403
            # Last-Modified must not cause a 304 with stale modules; no-store,
            # disabled etag and if_modified_since enforce complete fresh bytes.
            assert not request(prefix + 'app.mjs')[1].get('ETag')
            assert request(prefix + 'app.mjs', request_headers={'If-Modified-Since': 'Wed, 01 Jan 2098 00:00:00 GMT', 'If-None-Match': '*'})[0] == 200
            allowed_path = package / 'public/releases/m1-test/style.css'
            original = allowed_path.read_bytes()
            allowed_path.unlink()
            allowed_path.symlink_to(package / 'release-manifest.json')
            assert request(prefix + 'style.css')[0] in (403, 404)
            allowed_path.unlink()
            allowed_path.write_bytes(original)
            for protocol in (ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3):
                bounded = ssl.create_default_context(cafile=str(root / 'ca.pem'))
                bounded.minimum_version = bounded.maximum_version = protocol
                assert request(prefix + 'index.html', tls_context=bounded)[0] == 200
            try:
                request(prefix + 'index.html', tls_context=ssl.create_default_context())
            except ssl.SSLCertVerificationError:
                pass
            else:
                raise AssertionError('Untrusted test CA was unexpectedly accepted')
            for protocol in ('-tls1', '-tls1_1'):
                legacy = subprocess.run(['openssl', 's_client', protocol, '-connect', '127.0.0.1:' + str(port), '-cipher', 'ALL:@SECLEVEL=0'], input='', capture_output=True, text=True)
                assert legacy.returncode != 0 and 'alert protocol version' in legacy.stderr, legacy.stderr
            result = {'scope': 'synthetic loopback HTTPS only; no physical Bluetooth, public host or actual phone acceptance', 'nginx': run('nginx', '-v').stderr.strip(), 'runtime': checks, 'headers_verified_on_success_redirect_and_errors': HEADERS, 'denied_paths': denied, 'directory_listing': False, 'extra_public_canary_denied': True, 'symlink_to_private_manifest_denied': True, 'conditional_requests_return_fresh_200': True, 'manifest_outside_public': True, 'tls': {'TLS1.2': True, 'TLS1.3': True, 'TLS1.0_rejected': True, 'TLS1.1_rejected': True, 'untrusted_CA_rejected': True}, 'browser': None}
            if browser:
                result['browser'] = browser_acceptance(root, 'https://localhost:' + str(port) + prefix + 'index.html')
            assert len([p for p in (package / 'public').rglob('*') if p.is_file()]) == 5
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result))
        finally:
            process.terminate()
            try:
                process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()

def browser_acceptance(root, url):
    from playwright.sync_api import sync_playwright, Error, expect
    # NSS trust is scoped to disposable browser subprocess homes, never the
    # running user's/system trust stores. No certificate-error bypass flag.
    untrusted_profile = root / 'untrusted-browser'
    trusted_profile = root / 'trusted-browser'
    for directory in (untrusted_profile, trusted_profile):
        directory.mkdir()
    database = trusted_profile / '.pki/nssdb'
    database.mkdir(parents=True)
    run('certutil', '-N', '-d', 'sql:' + str(database), '--empty-password')
    run('certutil', '-A', '-d', 'sql:' + str(database), '-n', 'Ephemeral test CA only', '-t', 'C,,', '-i', root / 'ca.pem')
    with sync_playwright() as playwright:
        env = {**os.environ, 'HOME': str(untrusted_profile)}
        untrusted = playwright.chromium.launch(headless=True, args=['--no-sandbox'], env=env)
        page = untrusted.new_page()
        try:
            page.goto(url)
        except Error as error:
            assert 'ERR_CERT_AUTHORITY_INVALID' in str(error), str(error)
        else:
            raise AssertionError('Untrusted browser accepted ephemeral CA')
        untrusted.close()
        env = {**os.environ, 'HOME': str(trusted_profile)}
        browser = playwright.chromium.launch(headless=True, args=['--no-sandbox'], env=env)
        page = browser.new_page(viewport={'width': 390, 'height': 950})
        requests, errors, failed = [], [], []
        page.on('request', lambda request: requests.append(request.url))
        page.on('requestfailed', lambda request: failed.append({'url': request.url, 'failure': request.failure}))
        page.on('pageerror', lambda error: errors.append(str(error)))
        response = page.goto(url, wait_until='networkidle')
        assert response.status == 200
        assert page.evaluate('isSecureContext')
        bluetooth = page.evaluate("() => {const p=document.permissionsPolicy||document.featurePolicy;return {api: Boolean(navigator.bluetooth), recognized_policy: p.features().includes('bluetooth'), allowed_policy: p.allowsFeature('bluetooth')};}")
        if bluetooth['recognized_policy']:
            assert bluetooth['allowed_policy']
        if not bluetooth['api']:
            assert 'Android Chrome' in page.locator('#state-message').inner_text()
        assert page.locator('#device-card').is_hidden()
        assert page.locator('#state-pill').inner_text() != 'Secure test session'
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
        page.evaluate("() => { const s=document.createElement('script');s.textContent='window.cspInlineExecuted=true';document.head.append(s); }")
        assert not page.evaluate('Boolean(window.cspInlineExecuted)')
        page.evaluate("() => { const s=document.createElement('script');s.src='https://third-party.invalid/unapproved.js';document.head.append(s); }")
        page.wait_for_timeout(100)
        assert any(item['url'] == 'https://third-party.invalid/unapproved.js' and 'csp' in item['failure'].lower() for item in failed), failed
        # A CSP-blocked request can be reported as an attempted request by
        # Playwright; it must fail before a network connection is opened.
        actual = [request for request in requests if urlsplit(request).hostname == 'localhost']
        assert sorted(Path(urlsplit(request).path).name for request in actual) == sorted(RUNTIME), actual
        assert not errors, errors
        frame_page = browser.new_page()
        frame_page.set_content('<iframe src="' + url + '"></iframe>')
        frame_page.wait_for_timeout(200)
        assert all(frame.url != url for frame in frame_page.frames), [frame.url for frame in frame_page.frames]
        frame_page.close()
        version = browser.version
        browser.close()
    return {'version': version, 'trusted_scoped_test_CA': True, 'untrusted_CA_rejected': True, 'ignore_certificate_errors': False, 'secure_context': True, 'bluetooth': bluetooth, 'bluetooth_permissions_header': 'bluetooth=(self)', 'unsupported_browser_error_honest': not bluetooth['api'], 'inline_script_blocked': True, 'third_party_script_blocked': True, 'framing_blocked': True, 'only_five_runtime_assets_loaded': True, 'script_errors': errors, 'scope': 'real desktop Chromium, production five-file page; no GATT injection, no radio or Android acceptance'}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=REPO / 'artifacts/milestone1-corrections/phone-https-results.json')
    parser.add_argument('--browser', action='store_true')
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PackageTests)
    if not unittest.TextTestRunner().run(suite).wasSuccessful():
        raise SystemExit(1)
    integration(args.output, args.browser)

if __name__ == '__main__':
    main()
