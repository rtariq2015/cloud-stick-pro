"""Actual Chromium UI + browser crypto, mocked GATT against actual SDK C host.

This is desktop interoperability/visual testing, not a Bluetooth radio test.
"""
import functools,http.server,json,os,re,select,subprocess,threading
from pathlib import Path
from urllib.parse import urlsplit
from playwright.sync_api import sync_playwright,expect
root=Path(__file__).resolve().parents[1]
repo=root.parents[1]
out=Path(os.environ.get('CSP_PHONE_ARTIFACT_DIR',repo/'artifacts/milestone1/phone'));out.mkdir(parents=True,exist_ok=True)
host_binary=Path(os.environ.get('CSP_SECURITY2_HOST',repo/'artifacts/milestone1/host-security2/security2-host'))
handler=functools.partial(http.server.SimpleHTTPRequestHandler,directory=str(root))
class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        if urlsplit(self.path).path not in ('/','/index.html','/style.css','/app.mjs','/connection.mjs','/security2.mjs'):
            self.send_error(404);return
        super().do_GET()
    def do_HEAD(self):
        if urlsplit(self.path).path not in ('/','/index.html','/style.css','/app.mjs','/connection.mjs','/security2.mjs'):
            self.send_error(404);return
        super().do_HEAD()
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(QuietHandler,directory=str(root)))
threading.Thread(target=server.serve_forever,daemon=True).start()
url='http://127.0.0.1:'+str(server.server_port)
mock=(root/'test/gatt_mock.js').read_text()
results=[];process=None
try:
 with sync_playwright() as p:
  browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
  for width in [320,390,800]:
   page=browser.new_page(viewport={'width':width,'height':950});errors=[]
   page.on('pageerror',lambda e:errors.append(str(e)))
   for mode in ['idle','connected','error']:
    page.goto(url+'/?preview='+mode,wait_until='networkidle')
    assert page.locator('#preview-banner').is_visible()
    assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
    assert not errors,errors
    page.screenshot(path=str(out/(mode+'-'+str(width)+'.png')),full_page=True)
   page.close();results.append({'viewport':width,'preview_states':3,'script_errors':[],'horizontal_overflow':False})
  env={**os.environ,'ASAN_OPTIONS':os.environ.get('ASAN_OPTIONS','')+':halt_on_error=1:abort_on_error=1:detect_leaks=1','UBSAN_OPTIONS':os.environ.get('UBSAN_OPTIONS','')+':halt_on_error=1:print_stacktrace=1'}
  process=subprocess.Popen([str(host_binary)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env)
  diagnostics=[]
  diagnostic_reader=threading.Thread(target=lambda:diagnostics.extend(process.stderr),daemon=True);diagnostic_reader.start()
  def sdk(command,data):
   process.stdin.write(command+' '+data+'\n');process.stdin.flush()
   if not select.select([process.stdout],[],[],10)[0]:raise RuntimeError('SDK host response timed out')
   answer=process.stdout.readline()
   if not answer:raise RuntimeError('SDK host exited before answering')
   return json.loads(answer)
  page=browser.new_page(viewport={'width':390,'height':950});errors=[]
  page.on('pageerror',lambda e:errors.append(str(e)));page.expose_function('sdk',sdk);page.add_init_script(mock)
  page.goto(url,wait_until='networkidle');page.locator('#connect').click()
  try:expect(page.locator('#state-pill')).to_have_text('Secure test session',timeout=15000)
  except Exception:
   print(json.dumps({'ui_message':page.locator('#state-message').inner_text(),'script_errors':errors}));raise
  assert page.locator('#device-id').inner_text()=='CSP-M1-DESKTOP'
  assert page.locator('#preview-banner').is_visible()
  page.locator('#refresh').click();expect(page.locator('#state-message')).to_have_text('Device information refreshed.')
  assert not errors,errors
  assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
  page.screenshot(path=str(out/'desktop-sdk-interop-390.png'),full_page=True)
  page.evaluate("() => {dispatchEvent(new PageTransitionEvent('pagehide',{persisted:true}));dispatchEvent(new PageTransitionEvent('pageshow',{persisted:true}));}")
  expect(page.locator('#state-pill')).to_have_text('Disconnected');assert page.locator('#device-card').is_hidden()
  assert all(page.locator('#'+item).inner_text()=='' for item in ['device-title','device-id','firmware','wifi','backups'])
  page.evaluate("document.getElementById('refresh').click()")
  expect(page.locator('#state-message')).to_have_text('No authenticated session. Tap Find Cloud Stick to connect again.')
  assert sdk('reset','')['error']==0
  page.locator('#connect').click();expect(page.locator('#state-pill')).to_have_text('Secure test session')
  page.evaluate("() => {Object.defineProperty(document,'visibilityState',{configurable:true,value:'hidden'});document.dispatchEvent(new Event('visibilitychange'));Object.defineProperty(document,'visibilityState',{configurable:true,value:'visible'});document.dispatchEvent(new Event('visibilitychange'));}")
  expect(page.locator('#state-pill')).to_have_text('Disconnected');assert page.locator('#device-id').inner_text()==''
  assert sdk('reset','')['error']==0
  page.locator('#connect').click();expect(page.locator('#state-pill')).to_have_text('Secure test session')
  page.evaluate("() => {document.dispatchEvent(new Event('freeze'));document.dispatchEvent(new Event('resume'));}")
  expect(page.locator('#state-pill')).to_have_text('Disconnected');assert page.locator('#device-id').inner_text()==''
  assert sdk('reset','')['error']==0
  page.locator('#connect').click();expect(page.locator('#state-pill')).to_have_text('Secure test session')
  page.locator('#disconnect').click();assert page.locator('#device-card').is_hidden()
  process.stdin.write('quit\n');process.stdin.flush();assert process.wait(timeout=10)==0
  diagnostic_reader.join(timeout=5);assert not diagnostic_reader.is_alive()
  assert not re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|runtime error:',''.join(diagnostics),re.I),''.join(diagnostics)
  results.append({'actual_chromium_crypto_and_ui_against_sdk_c':True,'gatt':'mocked desktop transport with initially empty version response cache; no radio','authenticated_info_and_refresh':True,'disconnect':True,'restore_hidden_freeze_clear_identity':True,'new_user_click_and_authentication_required':True,'lifecycle_scope':'synthetic event dispatch in real desktop Chromium; not actual BFCache/phone acceptance','strict_sdk_sanitizer_stderr_checked':True,'script_errors':errors})
  browser.close()
 (out/'browser-results.json').write_text(json.dumps(results,indent=2)+'\n')
 print(json.dumps(results))
finally:
 server.shutdown()
 if process is not None and process.poll() is None:
  process.kill();process.wait(timeout=5)
