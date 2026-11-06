"""Desktop semantic/bounds checks; never a physical LCD acceptance claim."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
root=Path(__file__).resolve().parents[2];out=root/'artifacts/lcd-v2';out.mkdir(parents=True,exist_ok=True)
sources=[root/'include/pro_lcd_v2.h',root/'test/lcd_v2/model.test.cpp',Path(__file__)]
def hashes():return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
before=hashes();records=[]
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'}
commands=[['g++','-std=c++17','-Wall','-Wextra','-Werror','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'include'),str(root/'test/lcd_v2/model.test.cpp'),'-o',str(out/'model')],[str(out/'model')]]
for command in commands:
 p=subprocess.run(command,env=env,capture_output=True,text=True)
 diagnostic=bool(re.search(r'AddressSanitizer|LeakSanitizer|runtime error:|UndefinedBehaviorSanitizer|SUMMARY:',p.stdout+p.stderr))
 records.append({'exit':p.returncode,'diagnostic':diagnostic,'stdout':p.stdout,'stderr':p.stderr})
 if p.returncode or diagnostic or p.stderr:break
result={'passed':len(records)==2 and all(r['exit']==0 and not r['diagnostic'] and not r['stderr'] for r in records),'source_stable':hashes()==before,'sources':before,'records':records,'physical':False,'scope':'Frame semantics and bounded formatting; actual TFT glyphs/brightness/SPI appearance pending'}
(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ['sources','records']}));
if not result['passed'] or not result['source_stable']:
 print(json.dumps(records));sys.exit(1)
