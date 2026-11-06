"""Render the actual Android assets with public fake responses; no native/device API.

Uses the existing sandbox-enabled QtWebEngine runtime. The sole HTTP fixture
transformation removes app.mjs's automatic constructor so createViewer can use
injected synthetic responses. Production app/native/secured modules are unchanged.
"""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import hashlib,json,threading,time
from PySide6.QtCore import QTimer,QUrl,qVersion
from PySide6.QtWidgets import QApplication
from PySide6.QtWebEngineWidgets import QWebEngineView

ROOT=Path(__file__).resolve().parents[2]
ASSETS=ROOT/'android/app/src/main/assets'
OUT=ROOT/'artifacts/android-visual-20261106'
OUT.mkdir(parents=True,exist_ok=True)
ALLOW={'index.html':'text/html; charset=utf-8','style.css':'text/css; charset=utf-8',
       'app.mjs':'text/javascript; charset=utf-8','native.mjs':'text/javascript; charset=utf-8',
       'shared/connection.mjs':'text/javascript; charset=utf-8','shared/security2.mjs':'text/javascript; charset=utf-8'}
def sha(data):return hashlib.sha256(data).hexdigest()
before={name:sha((ASSETS/name).read_bytes()) for name in ALLOW}
BOOT="if(typeof document!=='undefined')createViewer();"
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  name=self.path.lstrip('/')
  if '?' in self.path or name not in ALLOW:self.send_error(404);return
  body=(ASSETS/name).read_bytes()
  if name=='app.mjs':
   text=body.decode();assert text.count(BOOT)==1;body=text.replace(BOOT,'// Synthetic preview constructs the actual viewer with injected public fixtures.').encode()
  self.send_response(200);self.send_header('Content-Type',ALLOW[name]);self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
app=QApplication([]);view=QWebEngineView();records=[];screenshots={};widths=[390,320];started=time.monotonic()

FIXTURE=r"""(async()=>{
 const {createViewer}=await import('./app.mjs');
 const info={firmware:'0.4.2-incremental-dev',wifi_connected:true,backup_status:true,file_history:true};
 const names=['QA/Example-report-01.pdf','QA/Example-report-02.pdf','QA/Example-notes.txt'];
 const calls=[];let options;
 const client={async connect(){options.onState({state:'connected'});return info;},async refresh(){return info;},disconnect(){options.onState({state:'disconnected'});},async command(op,args={}){
  calls.push(op);
  if(op==='backup_status')return {ok:true,cloud_configured:true,last_backup_unix:1793970600,verified:12,pending:0,failed:0,backup_error:0,backups_paused:true};
  if(op==='history_status')return {ok:true,g:1,n:3,matched:3,when:'1793970600',loaded:true,complete:true,limited:false,corrupt:false,io_failed:false,allocation_failed:false,exhausted:false};
  if(op==='history_row'){const bytes=new TextEncoder().encode(names[args.i]),end=Math.min(bytes.length,args.o+64);return {ok:true,g:1,i:args.i,n:3,b:args.i===2?'4096':'2097152',s:2,v:'1793970600',r:'0',e:0,a:0,p:bytes.length,o:args.o,x:end,m:end<bytes.length,h:Array.from(bytes.slice(args.o,end),x=>x.toString(16).padStart(2,'0')).join('')};}
  throw Error('Unexpected synthetic operation');
 }};
 createViewer({document,rpc:{onEvent(){},event(){},async request(op){if(op!=='permission')throw Error('Unexpected synthetic native operation');return {};}},bluetoothFactory:()=>({async drain(){},async close(){}}),connectionFactory:value=>{options=value;return client;}});
 document.getElementById('username').value='public-preview-owner';document.getElementById('password').value='PUBLIC-SYNTHETIC-PASSWORD';
 document.getElementById('login-form').requestSubmit();
 window.__previewReady=()=>!document.getElementById('result').hidden;
 window.__previewCalls=()=>calls.slice();
 window.__previewError='';
})().catch(error=>{window.__previewError='Synthetic fixture failed: '+error.message;});"""

def fail(message):
 (OUT/'results.json').write_text(json.dumps({'passed':False,'error':message,'records':records},indent=2)+'\n')
 print(message,flush=True);app.exit(1)
def check(name,condition):
 records.append({'name':name,'passed':bool(condition)})
 if not condition:raise AssertionError(name)
def image(name):
 path=OUT/name;check('render-'+name,view.grab().save(str(path),'PNG'));screenshots[name]={'sha256':sha(path.read_bytes()),'width':view.width(),'height':view.height()}
def inspect(expression,callback):
 # Qt returns JSON text consistently across its JavaScript QVariant bindings.
 view.page().runJavaScript("JSON.stringify("+expression+")",lambda value:callback(json.loads(value)))
def load_next():
 if not widths:return finish()
 width=widths[0];view.resize(width,844);view.load(QUrl(f'http://127.0.0.1:{server.server_port}/index.html'))
def loaded(ok):
 if not ok:return fail('Local assets failed to load')
 QTimer.singleShot(250,login_ready)
def login_ready():
 def reply(value):
  try:
   check('initial-private-results-hidden-'+str(widths[0]),value['hidden'])
   check('unique-ids-'+str(widths[0]),value['unique'])
   check('no-login-horizontal-overflow-'+str(widths[0]),value['fits'])
   check('large-login-control-'+str(widths[0]),value['control'])
   image('login-'+str(widths[0])+'.png');view.page().runJavaScript(FIXTURE.replace("'./app.mjs'",json.dumps(f"http://127.0.0.1:{server.server_port}/app.mjs")));wait_result()
  except Exception as error:fail(str(error))
 inspect("(()=>{const ids=[...document.querySelectorAll('[id]')].map(e=>e.id);return {hidden:document.getElementById('result').hidden&&document.getElementById('session-actions').hidden,unique:new Set(ids).size===ids.length,fits:document.documentElement.scrollWidth<=innerWidth,control:document.getElementById('connect').getBoundingClientRect().height>=44};})()",reply)
def wait_result():
 def reply(value):
  if value.get('error'):return fail(value['error'])
  if not value.get('ready'):
   if time.monotonic()-started>45:return fail('Synthetic result deadline')
   return QTimer.singleShot(100,wait_result)
  QTimer.singleShot(250,result_ready)
 inspect("({ready:typeof window.__previewReady==='function'&&window.__previewReady(),error:window.__previewError||''})",reply)
def result_ready():
 def reply(value):
  try:
   width=str(widths[0]);check('synthetic-recorded-count-'+width,value['count']=='12')
   check('synthetic-file-rows-'+width,value['rows']==3)
   check('no-result-horizontal-overflow-'+width,value['fits'])
   check('secondary-details-collapsed-'+width,value['collapsed'])
   check('primary-done-large-'+width,value['done'])
   check('private-content-inside-auth-container-'+width,value['private'])
   check('read-only-command-list-'+width,all(op in ['backup_status','history_status','history_row'] for op in value['calls']))
   image('results-'+width+'.png')
   view.page().runJavaScript("window.scrollTo(0,Math.max(0,document.querySelector('.records-card').offsetTop-20))")
   QTimer.singleShot(200,records_ready)
  except Exception as error:fail(str(error))
 inspect("(()=>({count:document.getElementById('count-verified').textContent,rows:document.getElementById('history').children.length,fits:document.documentElement.scrollWidth<=innerWidth,collapsed:!document.querySelector('.backup-details').open,done:document.getElementById('done').getBoundingClientRect().height>=44,private:['history','status','count-verified'].every(id=>document.getElementById(id).closest('#result')),calls:window.__previewCalls()}))()",reply)
def records_ready():
 image('records-'+str(widths[0])+'.png');view.page().runJavaScript("document.getElementById('done').click()")
 QTimer.singleShot(250,closed_ready)
def closed_ready():
 def reply(value):
  try:
   check('Done-clears-and-acknowledges-'+str(widths[0]),value)
   widths.pop(0);load_next()
  except Exception as error:fail(str(error))
 inspect("document.getElementById('result').hidden&&document.getElementById('history').children.length===0&&document.getElementById('password').value===''&&document.getElementById('connection-badge').textContent==='Bluetooth closed'",reply)
def finish():
 check('asset-source-stable',before=={name:sha((ASSETS/name).read_bytes()) for name in ALLOW})
 report={'passed':True,'renderer':'QtWebEngine Chromium','qt':qVersion(),'desktop_synthetic_only':True,'android_webview_physical_acceptance':False,'bluetooth_or_device_access':False,'external_network':False,'native_bridge':'public injected fake; no Android native calls','authentication':'not performed; synthetic client only','fixture_bootstrap_transformation':BOOT+' removed only from HTTP fixture response','source_sha256':before,'records':records,'screenshots':screenshots}
 (OUT/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'checks':len(records),'screenshots':list(screenshots)}),flush=True);app.exit(0)
view.loadFinished.connect(loaded);view.show();QTimer.singleShot(60000,lambda:fail('Preview deadline'));load_next()
code=app.exec();server.shutdown();server.server_close();raise SystemExit(code)
