"""Actual Chromium DOM/layout with an explicitly synthetic connection.

Every HTTPS response is intercepted locally. This is not deployment, TLS-origin
acceptance, physical GATT, Android, NVS or Koofr testing.
"""
import json
import mimetypes
from pathlib import Path
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'phone/wifi-setup'
OUT = ROOT / 'artifacts/cloud-setup-phone/browser'
OUT.mkdir(parents=True, exist_ok=True)
ORIGIN = 'https://csp-phone-synthetic.invalid'
FILES = {'index.html', 'style.css', 'app.mjs', 'connection.mjs', 'security2.mjs'}
fixture = r'''
// PUBLIC SYNTHETIC UI FIXTURE; no physical adapter, filesystem or server.
export class ManagementConnection {
 constructor(options){this.options=options;this.revision='4';this.edit=0;}
 async connect(){const info={device_id:'CSP-W2-SYNTHETIC',firmware:'SYNTHETIC',cloud_setup:true};this.options.onState({state:'connected',message:'Synthetic owner authenticated',info});return info;}
 disconnect(){this.options.onState({state:'disconnected',message:'Synthetic session closed'});}
 async command(op,args={}){
  if(op==='list')return {ok:true,revision:'2',count:0,profiles:[],next:0};
  if(op==='status')return {ok:true,revision:'2',durable:true,store_fault:false,wifi:'idle',wifi_error:0,wifi_profile:0,selected_profile:0,applied_revision:'0',internet_verified:false,cloud_available:false,backups_paused:true};
  const presence={has_username:true,has_password:true,has_folder:true};
  if(op==='cloud_status')return {ok:true,revision:this.revision,configured:true,provider:'koofr',...presence,editing:false,edit:0,store_fault:false};
  if(op==='cloud_begin'){this.edit=0;return {ok:true,revision:this.revision,transaction:'22',edit:0,...presence};}
  if(op==='cloud_stage')return {ok:true,transaction:'22',edit:++this.edit,...presence};
  if(op==='cloud_commit'){this.revision='5';return {ok:true,durable:true,revision:'5',access_verified:false};}
  if(op==='cloud_abort'||op==='abort')return {ok:true};
  throw new Error('Unexpected synthetic UI command');
 }
}
'''
requests = []

def respond(route):
    url = route.request.url
    requests.append(url)
    path = url.removeprefix(ORIGIN).split('?', 1)[0].lstrip('/') or 'index.html'
    if not url.startswith(ORIGIN + '/') or path not in FILES:
        raise AssertionError('Unexpected browser resource request: ' + url)
    body = (SOURCE / path).read_bytes()
    if path == 'connection.mjs':
        body = body.decode().replace('export class ManagementConnection {', 'class RealManagementConnection {', 1).encode() + fixture.encode()
    mime = 'application/javascript' if path.endswith('.mjs') else mimetypes.guess_type(path)[0]
    route.fulfill(status=200, content_type=mime + '; charset=utf-8', body=body)

results = []
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, args=['--no-sandbox'])
    for width in (320, 390, 800):
        page = browser.new_page(viewport={'width': width, 'height': 950})
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.route('**/*', respond)
        page.add_init_script("Object.defineProperty(navigator,'bluetooth',{value:{requestDevice(){throw new Error('Physical Bluetooth forbidden in fixture');}}});")
        page.goto(ORIGIN + '/index.html', wait_until='networkidle')
        expect(page.locator('#cloud-card')).to_be_hidden()
        page.locator('#owner-key').fill('PUBLIC_SYNTHETIC_OWNER_KEY_ONLY')
        page.locator('#connect').click()
        expect(page.locator('#cloud-card')).to_be_visible()
        page.locator('#cloud-keep-login').uncheck()
        page.locator('#cloud-keep-folder').uncheck()
        page.locator('#cloud-email').fill('owner@example.invalid')
        page.locator('#cloud-password').fill('PUBLIC_SYNTHETIC_APP_PASSWORD')
        page.locator('#cloud-folder').fill('CloudStick/QA')
        assert page.locator('#cloud-password').get_attribute('type') == 'password'
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
        page.locator('#cloud-card').screenshot(path=str(OUT / f'fields-{width}.png'))
        page.locator('#cloud-review').click()
        expect(page.locator('#cloud-confirm')).to_be_visible()
        expect(page.locator('#cloud-password')).to_have_value('')
        expect(page.locator('#cloud-save')).to_be_disabled()
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
        page.locator('#cloud-accept').check()
        page.locator('#cloud-save').click()
        expect(page.locator('#cloud-message')).to_contain_text('not yet verified')
        page.locator('#cloud-card').screenshot(path=str(OUT / f'saved-{width}.png'))
        page.locator('#cloud-finish').click()
        expect(page.locator('#cloud-card')).to_be_hidden()
        expect(page.locator('#cloud-email')).to_have_value('')
        expect(page.locator('#cloud-password')).to_have_value('')
        expect(page.locator('#cloud-folder')).to_have_value('')
        assert not errors, errors
        results.append({'width': width, 'horizontal_overflow': False, 'script_errors': [], 'review_save_disconnect': 'pass'})
        page.close()
    browser.close()

report = {'passed': True, 'viewports': results, 'resources': sorted(set(requests)),
          'fixture_connection': True, 'real_browser': 'Chromium desktop', 'physical_bluetooth': False,
          'real_cloud': False, 'android_acceptance': False, 'deployment': False, 'tls_origin_acceptance': False}
(OUT / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS synthetic cloud form in Chromium at 320/390/800px: no overflow/errors, review/consent/save/clear; all five resources intercepted locally')
