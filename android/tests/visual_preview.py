"""Render actual Android assets with public Wi-Fi/BLE fixtures, never a device.

Only the HTTP fixture removes the auto-constructor. Qt's sandbox stays enabled.
Native Android permissions, transport, persistence and WebView need phone testing.
"""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import hashlib,json,threading,time
from PySide6.QtCore import QTimer,QUrl,qVersion
from PySide6.QtWidgets import QApplication
from PySide6.QtWebEngineWidgets import QWebEngineView
ROOT=Path(__file__).resolve().parents[2]
ASSETS=ROOT/'android/app/src/main/assets';OUT=ROOT/'artifacts/android-wifi-visual'
OUT.mkdir(parents=True,exist_ok=True)
ALLOW={name:('text/css' if name.endswith('.css') else 'text/html' if name.endswith('.html') else 'text/javascript') for name in ['index.html','style.css','app.mjs','dashboard.mjs','native.mjs','shared/connection.mjs','shared/security2.mjs']}
def sha(data):return hashlib.sha256(data).hexdigest()
before={name:sha((ASSETS/name).read_bytes()) for name in ALLOW}
BOOT="if(typeof document!=='undefined')createApplication({viewerFactory:createViewer});"
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  name=self.path.lstrip('/')
  if '?' in self.path or name not in ALLOW:self.send_error(404);return
  body=(ASSETS/name).read_bytes()
  if name=='app.mjs':
   text=body.decode();assert text.count(BOOT)==1;body=text.replace(BOOT,'// Public fixture supplies injected transports.').encode()
  self.send_response(200);self.send_header('Content-Type',ALLOW[name]+'; charset=utf-8');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
app=QApplication([]);view=QWebEngineView();records=[];screenshots={};widths=[390,320];started=time.monotonic()
FIXTURE=r"""(async()=>{
 const {createViewer}=await import('./app.mjs');const {createApplication}=await import('./dashboard.mjs');
 const state={firmware:'0.4.6-cache-dev',uptime_ms:5400000,wifi:true,media:true,active:true,scanning:false,has_scan:true,scan_complete:false,window_open:true,paused:false,cloud:true,configured:true,verified:12,pending:2,failed:0,done:'1048576',total:'2097152',bytes_per_second:51200,quiet_seconds:0,last_backup_unix:1794000000,error:0,percent:50,phase:2};
 const info={firmware:state.firmware,wifi_connected:true,backup_status:true,file_history:true},calls=[];let options,offline=false;
 const names=['QA/Example-report-01.pdf','QA/Example-report-02.pdf','QA/Example-notes.txt'];
 const client={async connect(){options.onState({state:'connected'});return info;},async refresh(){return info;},disconnect(){options.onState({state:'disconnected'});},async command(op,args={}){
  calls.push(op);
  if(op==='backup_status')return {ok:true,cloud_configured:true,last_backup_unix:1794000000,verified:12,pending:0,failed:0,backup_error:0,backups_paused:true};
  if(op==='history_status')return {ok:true,g:1,n:3,matched:3,when:'1794000000',loaded:true,complete:true,limited:false,corrupt:false,io_failed:false,allocation_failed:false,exhausted:false};
  if(op==='history_row'){const bytes=new TextEncoder().encode(names[args.i]),end=Math.min(bytes.length,args.o+64);return {ok:true,g:1,i:args.i,n:3,b:args.i===2?'4096':'2097152',s:2,v:'1794000000',r:'0',e:0,a:0,p:bytes.length,o:args.o,x:end,m:end<bytes.length,h:Array.from(bytes.slice(args.o,end),x=>x.toString(16).padStart(2,'0')).join('')};}
  throw Error('Unexpected synthetic command');
 }};
 const rpc={event(){},async request(op,args){calls.push(op);if(op==='dashboard_address')return {address:'192.168.1.42'};if(op==='dashboard_status'){if(offline)throw Error('synthetic offline');return state;}if(op==='dashboard_save')return {address:args.address};if(['dashboard_cancel','permission'].includes(op))return {};throw Error('Unexpected synthetic native operation');}};
 const application=createApplication({document,rpc,viewerFactory:args=>createViewer({...args,bluetoothFactory:()=>({async drain(){},async close(){}}),connectionFactory:value=>{options=value;return client;}})});
 await application.ready;window.__preview={application,calls,offline:()=>{offline=true;}};window.__previewReady=true;
})().catch(error=>{window.__previewError=error.message;});"""
def fail(message):
 (OUT/'results.json').write_text(json.dumps({'passed':False,'error':message,'records':records},indent=2)+'\n');print(message,flush=True);app.exit(1)
def check(name,condition):
 records.append({'name':name+'-'+str(widths[0]),'passed':bool(condition)})
 if not condition:raise AssertionError(name)
def image(name):
 path=OUT/(name+'-'+str(widths[0])+'.png');check('render-'+name,view.grab().save(str(path),'PNG'));screenshots[path.name]={'sha256':sha(path.read_bytes()),'width':view.width(),'height':view.height()}
def js(code):view.page().runJavaScript(code)
def inspect(expression,callback):
 view.page().runJavaScript('JSON.stringify('+expression+')',lambda value:callback(json.loads(value)))
def load_next():
 if not widths:return finish()
 view.resize(widths[0],844);view.load(QUrl(f'http://127.0.0.1:{server.server_port}/index.html'))
def loaded(ok):
 if not ok:return fail('Local assets failed to load')
 fixture=FIXTURE
 for name in ['app.mjs','dashboard.mjs']:fixture=fixture.replace("'./"+name+"'",json.dumps(f'http://127.0.0.1:{server.server_port}/'+name))
 js(fixture);QTimer.singleShot(250,wait_ready)
def wait_ready():
 def reply(value):
  if value['error']:return fail(value['error'])
  if not value['ready']:
   if time.monotonic()-started>50:return fail('Fixture deadline')
   return QTimer.singleShot(100,wait_ready)
  dashboard_ready()
 inspect("({ready:!!window.__previewReady,error:window.__previewError||''})",reply)
def dashboard_ready():
 def reply(v):
  try:
   check('no-horizontal-overflow',v['fits']);check('private-pane-hidden',v['private']);check('unique-ids',v['unique']);check('live-count',v['count']=='12');check('no-bluetooth-or-login-on-home',v['calls']==['dashboard_address','dashboard_status']);check('visible-live-progress',v['progress']);check('refresh-touch-size',v['refresh']);image('dashboard')
   js("window.__preview.application.switchMode('private')");QTimer.singleShot(200,login_ready)
  except Exception as error:fail(str(error))
 inspect("(()=>{const ids=[...document.querySelectorAll('[id]')].map(e=>e.id);return {fits:document.documentElement.scrollWidth<=innerWidth,private:document.getElementById('private-pane').hidden,unique:new Set(ids).size===ids.length,count:document.getElementById('live-verified').textContent,calls:window.__preview.calls,progress:!document.getElementById('live-progress').hidden,refresh:document.getElementById('live-refresh').getBoundingClientRect().height>=44};})()",reply)
def login_ready():
 def reply(v):
  try:
   check('login-fits',v['fits']);check('private-results-hidden-before-auth',v['hidden']);check('large-signin-control',v['control']);image('login')
   js("document.getElementById('username').value='public-preview-owner';document.getElementById('password').value='PUBLIC-SYNTHETIC-PASSWORD';document.getElementById('login-form').requestSubmit()")
   QTimer.singleShot(300,result_ready)
  except Exception as error:fail(str(error))
 inspect("({fits:document.documentElement.scrollWidth<=innerWidth,hidden:document.getElementById('result').hidden,control:document.getElementById('connect').getBoundingClientRect().height>=44})",reply)
def result_ready():
 def reply(v):
  try:
   check('authenticated-result-visible',v['visible']);check('recorded-count',v['count']=='12');check('three-file-rows',v['rows']==3);check('private-results-fit',v['fits']);check('done-touch-size',v['done']);check('optional-details-collapsed',v['collapsed']);image('results')
   js("window.scrollTo(0,Math.max(0,document.querySelector('.records-card').offsetTop-20))");QTimer.singleShot(150,records_ready)
  except Exception as error:fail(str(error))
 inspect("({visible:!document.getElementById('result').hidden,count:document.getElementById('count-verified').textContent,rows:document.getElementById('history').children.length,fits:document.documentElement.scrollWidth<=innerWidth,done:document.getElementById('done').getBoundingClientRect().height>=44,collapsed:!document.querySelector('.backup-details').open})",reply)
def records_ready():
 image('records');js("window.__preview.application.switchMode('dashboard');window.scrollTo(0,0)");QTimer.singleShot(250,back_ready)
def back_ready():
 def reply(v):
  try:
   check('return-clears-private-and-closes-BLE',v);js("window.__preview.offline();document.getElementById('live-refresh').click()");QTimer.singleShot(250,offline_ready)
  except Exception as error:fail(str(error))
 inspect("document.getElementById('private-pane').hidden&&document.getElementById('result').hidden&&document.getElementById('history').children.length===0&&document.getElementById('password').value===''&&document.getElementById('connection-badge').textContent==='Bluetooth closed'",reply)
def offline_ready():
 def reply(v):
  try:
   check('offline-counts-marked-stale',v['stale']);check('offline-progress-hidden',v['progress']);check('offline-fits',v['fits']);image('offline')
   js("document.getElementById('forget-address').click();window.scrollTo(0,0)");QTimer.singleShot(200,first_ready)
  except Exception as error:fail(str(error))
 inspect("({stale:document.getElementById('dashboard-badge').textContent==='Not connected'&&document.getElementById('live-updated').textContent.includes('previous read'),progress:document.getElementById('live-progress').hidden,fits:document.documentElement.scrollWidth<=innerWidth})",reply)
def first_ready():
 def reply(v):
  try:
   check('forgotten-address-clears-counts',v['clear']);check('first-launch-address-form-open',v['open']);image('first-launch');widths.pop(0);load_next()
  except Exception as error:fail(str(error))
 inspect("({clear:document.getElementById('live-verified').textContent==='—',open:document.getElementById('address-details').open})",reply)
def finish():
 assert before=={name:sha((ASSETS/name).read_bytes()) for name in ALLOW}
 report={'passed':True,'renderer':'QtWebEngine Chromium','qt':qVersion(),'desktop_synthetic_only':True,'android_webview_physical_acceptance':False,'bluetooth_or_device_access':False,'external_network':False,'native_bridge':'injected public Wi-Fi/BLE fixtures','authentication':'not performed; fake authenticated client','fixture_bootstrap_transformation':BOOT+' removed only from fixture response','source_sha256':before,'records':records,'screenshots':screenshots}
 (OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'checks':len(records),'screenshots':len(screenshots)}),flush=True);app.exit(0)
view.loadFinished.connect(loaded);view.show();QTimer.singleShot(60000,lambda:fail('Preview deadline'));load_next();code=app.exec();server.shutdown();server.server_close();raise SystemExit(code)
