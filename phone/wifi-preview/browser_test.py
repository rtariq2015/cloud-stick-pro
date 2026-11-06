"""Real desktop Chromium; local fixture UI only. Never contacts a dongle/cloud."""
from functools import partial
from http.server import ThreadingHTTPServer
from pathlib import Path
from threading import Thread
import json
import resource
from playwright.sync_api import sync_playwright, expect
from serve import Handler

root = Path(__file__).resolve().parent
out = root.parents[1] / 'artifacts/wifi-candidate/browser'
out.mkdir(parents=True, exist_ok=True)
server = ThreadingHTTPServer(('127.0.0.1', 0), partial(Handler, directory=str(root)))
Thread(target=server.serve_forever, daemon=True).start()
url = f'http://127.0.0.1:{server.server_port}'
results = []
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
try:
    with sync_playwright() as p:
        # Require Chromium's own sandbox; do not work around a failed sandbox.
        browser = p.chromium.launch(headless=True, chromium_sandbox=True)
        for width in [320, 390, 800]:
            page = browser.new_page(viewport={'width': width, 'height': 950})
            errors, violations, network = [], [], []
            page.on('pageerror', lambda e: errors.append(str(e)))
            page.on('request', lambda r: network.append(r.url))
            page.add_init_script("""globalThis.policyViolations=[];document.addEventListener('securitypolicyviolation',e=>policyViolations.push(e.violatedDirective));""")
            response = page.goto(url, wait_until='networkidle')
            assert response.headers['permissions-policy'].startswith('bluetooth=()')
            assert page.locator('#candidate').is_hidden()
            page.locator('#scan').click()
            expect(page.locator('#networks .network')).to_have_count(3)
            assert page.get_by_role('button', name='CSP Test Enterprise').is_disabled()
            page.get_by_role('button', name='CSP Test Private').click()
            assert page.locator('#password').input_value() == 'testpass123'
            assert page.locator('#password').get_attribute('readonly') is not None
            page.locator('#outcome').select_option('wrong_password')
            page.locator('#test').click()
            expect(page.locator('#check-result')).to_contain_text('password was rejected')
            assert page.locator('#save').is_hidden()
            assert page.locator('#active-network').inner_text() == 'CSP Test Home'
            page.screenshot(path=str(out / f'wrong-password-{width}.png'), full_page=True)
            page.locator('#outcome').select_option('ready')
            page.locator('#test').click()
            expect(page.locator('#save')).to_be_visible()
            assert page.locator('#active-network').inner_text() == 'CSP Test Home'
            page.screenshot(path=str(out / f'ready-{width}.png'), full_page=True)
            page.locator('#save').click()
            assert page.locator('#active-network').inner_text() == 'CSP Test Private'
            assert page.locator('#password').input_value() == ''
            page.get_by_role('button', name='CSP Test Guest').click()
            assert page.locator('#password-row').is_hidden()
            assert page.locator('#open-note').is_visible()
            page.locator('#outcome').select_option('captive')
            page.locator('#test').click()
            expect(page.locator('#check-result')).to_contain_text('Accept page')
            assert page.locator('#save').is_hidden()
            page.locator('#manual').click()
            assert page.locator('#selected-name').inner_text() == 'CSP Test Hidden'
            page.locator('#test').click()
            page.evaluate("document.dispatchEvent(new Event('freeze'))")
            page.wait_for_timeout(400)
            assert page.locator('#candidate').is_hidden()
            assert page.locator('#password').input_value() == ''
            assert page.locator('#save').is_hidden()
            page.evaluate("dispatchEvent(new PageTransitionEvent('pageshow',{persisted:true}))")
            assert page.locator('#candidate').is_hidden()
            assert not page.evaluate('Object.keys(localStorage).length || sessionStorage.length')
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
            assert not errors, errors
            assert not page.evaluate('policyViolations')
            assert all(item.startswith(url + '/') for item in network), network
            page.reload(wait_until='networkidle')
            assert page.locator('#active-network').inner_text() == 'CSP Test Home'
            results.append({'width': width, 'passed': True, 'script_errors': errors,
                'network': 'loopback static files only', 'storage_empty': True,
                'sandbox': True, 'horizontal_overflow': False,
                'flows': ['scan', 'unsupported', 'private', 'wrong-password', 'ready',
                          'explicit-save', 'open', 'captive', 'hidden', 'late-result-freeze',
                          'restoration', 'reload-discards-demo']})
            page.close()
        browser.close()
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results))
except Exception as error:
    (out / 'results.json').write_text(json.dumps({
        'passed': False, 'completed_viewports': results,
        'error': str(error), 'sandbox_disabled': False,
        'scope': 'Browser verification incomplete; do not treat DOM simulation as visual acceptance'
    }, indent=2) + '\n')
    raise
finally:
    server.shutdown()
    server.server_close()
