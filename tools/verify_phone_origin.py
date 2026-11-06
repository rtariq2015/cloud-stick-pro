"""Public exact-origin TLS/hash/MIME/header checks; no login/token/cookie use."""
from pathlib import Path
import hashlib,http.client,json,ssl
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/backup-wifi-integration/phone-site'
manifest=json.loads((OUT/'release-manifest.json').read_text())
host='cloud-stick-bluetooth-test.rtariq.chatgpt.site'
prefix='/releases/'+manifest['release']+'/'
rows=[]
def fetch(path,method='GET'):
    connection=http.client.HTTPSConnection(host,timeout=20,context=ssl.create_default_context())
    try:
        connection.request(method,path,headers={'User-Agent':'CloudStick-ReleaseVerifier/1.0',
                            'Accept-Encoding':'identity','Connection':'close'})
        response=connection.getresponse();body=response.read(131073)
        assert len(body)<=131072
        return response.status,body,{key:response.getheader(key) for key in
            [*manifest['headers'],'Content-Type','Content-Encoding','Location']}
    finally:connection.close()
status,body,headers=fetch('/')
assert status==302 and headers['Location']==prefix+'index.html'
rows.append({'path':'/','status':status})
for name,wanted in manifest['files'].items():
    status,body,headers=fetch(prefix+name)
    assert status==200 and headers['Content-Encoding'] in [None,'identity']
    assert len(body)==wanted['bytes'] and hashlib.sha256(body).hexdigest()==wanted['sha256']
    assert headers['Content-Type']==wanted['mime']
    assert all(headers[key]==value for key,value in manifest['headers'].items())
    rows.append({'path':prefix+name,'status':status,'sha256':wanted['sha256'],'headers':headers})
for previous in ['wifi-m2-phone-20261104-r1','wifi-m2-phone-20261104-r2','backup-wifi-phone-20261104-r3','backup-wifi-phone-20261104-r4']:
    status,body,headers=fetch('/releases/'+previous+'/index.html')
    assert status==302 and headers['Location']==prefix+'index.html'
    rows.append({'previous':previous,'redirects_to_current':True})
for path in ['/owner-card.json','/firmware.bin','/.git/config',prefix+'owner-card.json']:
    status,body,headers=fetch(path);assert status in [403,404]
    rows.append({'path':path,'status':status})
for method in ['POST','PUT','DELETE','PATCH']:
    status,body,headers=fetch(prefix+'index.html',method);assert status in [403,405]
    rows.append({'method':method,'status':status})
result={'passed':True,'origin':'https://'+host,'release':manifest['release'],
        'public_without_login':True,'authorization_or_cookies_sent':False,
        'tls_hostname_certificate_validated':True,'five_asset_hashes_mime_headers_verified':True,'rows':rows}
(OUT/('origin-public-checks-'+manifest['release']+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({key:value for key,value in result.items() if key!='rows'}))
