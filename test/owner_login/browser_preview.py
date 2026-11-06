"""Actual Chromium UI/WebCrypto, public synthetic account and local transport.

All five HTTPS resources are intercepted locally. No device, browser password
manager acceptance, deployed TLS origin or real credential is exercised.
"""
import json
import mimetypes
from pathlib import Path
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'phone/wifi-setup'
OUT = ROOT / 'artifacts/owner-login-phone/browser'
OUT.mkdir(parents=True, exist_ok=True)
ORIGIN = 'https://owner-login-synthetic.invalid'
FILES = {'index.html', 'style.css', 'app.mjs', 'connection.mjs', 'security2.mjs'}
fixture = r'''
// PUBLIC SYNTHETIC TRANSPORT. Actual record generation uses browser WebCrypto.
export class ManagementConnection {
 constructor(options){this.options=options;this.edit=0;this.received=0;window.fixtureFacts={stages:0,commits:0,legacy:!!options.ownerKey};}
 async connect(){const info={device_id:'CSP-W2-001122334455',firmware:'SYNTHETIC',owner_login:true};this.options.onState({state:'connected',message:'Synthetic owner authenticated',info});return info;}
 disconnect(){this.options.onState({state:'disconnected',message:'Synthetic session closed'});}
 async reserveOwnerLogin(){}
 answer(overrides={}){return {ok:true,error:0,transaction:'99',revision:'1001',edit:this.edit,received:this.received,device:'001122334455',durable:false,reconnect_required:false,store_fault:false,...overrides};}
 async command(op,args={}){
  if(op==='list')return {ok:true,revision:'2',count:0,profiles:[],next:0};
  if(op==='status')return {ok:true,revision:'2',durable:true,store_fault:false,wifi:'idle',wifi_error:0,wifi_profile:0,selected_profile:0,applied_revision:'0',internet_verified:false,cloud_available:false,backups_paused:true};
  if(op==='abort')return {ok:true};
  if(op==='login_begin'){this.edit=this.received=0;return this.answer();}
  if(op==='login_stage'){if(args.offset!==this.received||args.edit!==this.edit||args.data.length>256)throw Error('Incorrect synthetic delivery');++this.edit;this.received+=args.data.length/2;++window.fixtureFacts.stages;return this.answer();}
  if(op==='login_commit'){if(this.received!==432||!args.accept)throw Error('Unconfirmed synthetic commit');++window.fixtureFacts.commits;return this.answer({revision:'427',durable:true,reconnect_required:true});}
  if(op==='login_abort'){this.edit=this.received=0;return this.answer();}
  throw Error('Unexpected synthetic operation');
 }
}
'''
requests = []

def respond(route):
    url = route.request.url
    requests.append(url)
    path = url.removeprefix(ORIGIN).lstrip('/') or 'index.html'
    if not url.startswith(ORIGIN + '/') or path not in FILES:
        raise AssertionError('Unexpected preview request: ' + url)
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
        page.add_init_script("Object.defineProperty(navigator,'bluetooth',{value:{requestDevice(){throw Error('Physical Bluetooth forbidden');}}});")
        page.goto(ORIGIN + '/index.html', wait_until='networkidle')
        page.evaluate("""() => {const label=document.createElement('div');label.textContent='SYNTHETIC PREVIEW · no USB connection';label.style.cssText='background:#e9dcae;color:#4d4528;text-align:center;padding:10px;font:600 13px system-ui';document.body.prepend(label);} """)
        expect(page.locator('#owner-login-card')).to_be_hidden()
        expect(page.locator('#login-username')).to_have_value('')
        assert page.locator('#login-password').get_attribute('autocomplete') == 'current-password'
        assert page.locator('#existing-key').get_attribute('open') is None
        page.screenshot(path=str(OUT / f'synthetic-sign-in-{width}.png'), full_page=True)
        page.locator('#login-username').fill('PUBLIC_USER')
        page.locator('#login-password').fill('PUBLIC_OLD_PASSWORD')
        page.locator('#login-password').press('Enter')
        expect(page.locator('#owner-login-card')).to_be_visible()
        expect(page.locator('#new-login-username')).to_have_value('admin')
        expect(page.locator('#login-password')).to_have_value('')
        page.locator('#new-login-password').fill('PUBLIC-SYNTHETIC-LOGIN-2026')
        page.locator('#new-login-confirm').fill('PUBLIC-SYNTHETIC-LOGIN-2026')
        page.locator('#owner-login-review').click()
        expect(page.locator('#owner-login-confirm-panel')).to_be_visible()
        expect(page.locator('#new-login-password')).to_have_value('')
        expect(page.locator('#new-login-confirm')).to_have_value('')
        expect(page.locator('#owner-login-save')).to_be_disabled()
        assert page.evaluate('fixtureFacts.stages') == 4
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
        page.locator('#owner-login-card').screenshot(path=str(OUT / f'synthetic-review-{width}.png'))
        page.locator('#owner-login-accept').check()
        page.locator('#owner-login-save').click()
        expect(page.locator('#owner-login-outcome')).to_contain_text('new login was saved')
        expect(page.locator('#owner-login-card')).to_be_hidden()
        assert page.evaluate('fixtureFacts.commits') == 1
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
        assert not errors, errors
        results.append(dict(width=width, sign_in_form='pass', real_webcrypto_record='pass', stage_chunks=4,
                            review_consent_commit='pass', teardown='pass', horizontal_overflow=False, script_errors=[]))
        page.close()
    # Legacy entry stays available in a closed details element.
    page = browser.new_page(viewport={'width': 390, 'height': 950})
    page.route('**/*', respond)
    page.add_init_script("Object.defineProperty(navigator,'bluetooth',{value:{requestDevice(){}}});")
    page.goto(ORIGIN + '/index.html', wait_until='networkidle')
    page.locator('#existing-key summary').click()
    page.locator('#owner-key').fill('PUBLIC_EXISTING_OWNER_KEY_ONLY_XX')
    page.locator('#connect').click()
    expect(page.locator('#owner-login-card')).to_be_visible()
    assert page.evaluate('fixtureFacts.legacy') is True
    page.close()
    browser.close()

report = dict(passed=True, actual_browser='Chromium desktop', real_webcrypto=True, synthetic_transport=True,
              resources=sorted(set(requests)), viewports=results, legacy_details_path='pass',
              device_contact=False, deployed_origin=False, actual_password_manager=False, android_acceptance=False)
(OUT / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS Chromium normal sign-in/new-login review at 320/390/800px with real WebCrypto, explicit consent, teardown and legacy path; five local resources only')
