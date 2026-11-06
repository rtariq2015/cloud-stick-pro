"""Real Chromium checks against the firmware page, with synthetic API data."""
import json,re
from pathlib import Path
from playwright.sync_api import sync_playwright
root=Path(__file__).resolve().parents[1]
src=(root/'include/portal_ui.h').read_text()
page_html=re.search(r'Page\[\] PROGMEM=R"HTML\((.*?)\)HTML";',src,re.S).group(1)
script=re.search(r'Script\[\] PROGMEM=R"JS\((.*?)\)JS";',src,re.S).group(1)
version=re.search(r'FirmwareVersion="([^"]+)"',(root/'include/pro_device.h').read_text()).group(1).replace('-dev','')
out=root/'artifacts'/version/'previews';out.mkdir(parents=True,exist_ok=True)
state=dict(configured=True,dashboard_only=True,csrf='synthetic-token',firmware=version+'-dev',wifi_connected=True,has_scan_since_boot=True,backup_active=True,scan_complete=False,transfer_phase=2,progress_percent=63,transfer_done_bytes=1321205,transfer_total_bytes=2097152,transfer_bytes_per_second=65536,watch_folder='QA',verified=12,pending=2,failed=0,last_backup_unix=1793680000,backup_window_open=True,maintenance=False,backup_mode='idle',message='Your original files stay on the USB.',storage='USB available; automatic backups',station_ip='192.168.1.42',cloud_help='Cloud login and folder access passed.')
state['current_file']='QA/'+('long-name-'*18)+'.pdf'
files=[dict(path='QA/CT daily quality check.pdf',state='verified',bytes=2097152,verified_unix=1793680000),dict(path='QA/November 03 report.pdf',state='checking',bytes=1048576),dict(path='QA/<img src=x onerror=alert(1)>.pdf',state='retry',bytes=2000,error_code=503,retry_unix=1793680300)]
with sync_playwright() as p:
 browser=p.chromium.launch(headless=True,args=['--no-sandbox'])
 for width in (320,390,800):
  page=browser.new_page(viewport=dict(width=width,height=950));errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
  def route(r):
   path=r.request.url.split('192.168.1.42')[-1]
   if path=='/':r.fulfill(content_type='text/html',body=page_html)
   elif path=='/app.js':r.fulfill(content_type='application/javascript',body=script)
   elif path=='/status':r.fulfill(json=state)
   elif path=='/history':r.fulfill(json=dict(files=files,more=False,cursor=''))
   else:r.fulfill(status=403,body='Read only')
  page.route('**/*',route);page.goto('http://192.168.1.42/');page.wait_for_selector('#file-history li')
  assert page.locator('#backup-headline').inner_text()=='Uploading to Koofr'
  assert page.locator('#backup-description').inner_text()==state['current_file']
  assert page.locator('#file-history img').count()==0
  assert page.evaluate('document.documentElement.scrollWidth<=innerWidth'),width
  assert page.locator('#start-backup').is_hidden()
  page.screenshot(path=str(out/f'dashboard-{width}.png'),full_page=True)
  assert not errors,errors
  page.close()
 browser.close()
print('Chromium dashboard: 320/390/800px, safe filenames, read-only controls, no overflow or script errors: PASS')
