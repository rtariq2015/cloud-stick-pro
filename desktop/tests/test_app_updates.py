# SPDX-License-Identifier: MIT
import base64
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cloudstick_desktop import app_updates as updates
from cloudstick_desktop.lan import address
from cloudstick_desktop.protocol import validate_request,TransportError

class AppUpdateTests(unittest.TestCase):
    def setUp(self):
        self.key=ec.generate_private_key(ec.SECP256R1())
        self.public=self.key.public_key().public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo)
        self.data={'schema':'cloud-stick-app-catalog/v1','revision':4,'releases':[{'platform':'windows','version':'1.2.3','code':1,'tag':'preview-test','asset':'cloud-stick-test.exe','size':1024,'sha256':'a'*64,'source':'b'*40}]}
    def signed(self,data=None):
        payload=json.dumps(data or self.data).encode();signature=self.key.sign(updates.DOMAIN+payload,ec.ECDSA(hashes.SHA256()))
        return json.dumps({'schema':'cloud-stick-app-signature/v1','payload':base64.b64encode(payload).decode(),'signature':base64.b64encode(signature).decode()}).encode()
    def test_signature_revision_and_exact_metadata(self):
        raw=self.signed();self.assertEqual(updates.catalog(raw,4,self.public),self.data)
        for value,minimum in [(raw,5),(raw.replace(b'cloud-stick-app-signature/v1',b'other'),1)]:
            with self.assertRaises(Exception):updates.catalog(value,minimum,self.public)
        envelope=json.loads(raw);envelope['payload']=base64.b64encode(b'changed').decode()
        with self.assertRaises(Exception):updates.catalog(json.dumps(envelope).encode(),key=self.public)
    def test_signed_dangerous_asset_paths_and_duplicate_platforms_rejected(self):
        for field,value in [('asset','../cloud-stick-test.exe'),('tag','tag/../../x'),('size',True),('sha256','bad'),('asset','cloud-stick-test.zip')]:
            data=copy.deepcopy(self.data);data['releases'][0][field]=value
            with self.subTest(field=field,value=value),self.assertRaises(Exception):updates.catalog(self.signed(data),key=self.public)
        data=copy.deepcopy(self.data);data['releases']*=2
        with self.assertRaises(Exception):updates.catalog(self.signed(data),key=self.public)
    def test_installation_rechecks_download_and_rejects_change(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'cloud-stick-test.exe';path.write_bytes(b'x'*1024)
            manager=updates.AppUpdates(Path(directory),'windows');manager.path=path
            manager.release={**self.data['releases'][0],'sha256':hashlib.sha256(path.read_bytes()).hexdigest()};manager.status['phase']='ready'
            self.assertEqual(manager.installation()[0],path)
            path.write_bytes(b'y'*1024)
            with self.assertRaises(ValueError):manager.installation()
    def test_renderer_cannot_choose_installer_url_or_path(self):
        for op in ('app_update_check','app_update_state','app_update_download'):
            self.assertEqual(validate_request({'op':op}),op)
            for field in ('url','path','sha256','approved'):
                with self.assertRaises(TransportError):validate_request({'op':op,field:'synthetic'})
    def test_lan_is_rfc1918_only_and_rejects_urls(self):
        for value in ('10.0.0.1','172.16.0.1','192.168.1.42'):self.assertEqual(address(value),value)
        for value in ('127.0.0.1','8.8.8.8','169.254.1.1','http://192.168.1.42','192.168.001.1','192.168.1.42:80'):
            with self.subTest(value=value),self.assertRaises(ValueError):address(value)
    def test_public_wire_u64_is_exactly_normalized_and_redirect_never_followed(self):
        from cloudstick_desktop import lan
        from unittest.mock import MagicMock
        data={k:False for k in ('wifi','media','active','scanning','has_scan','scan_complete','window_open','paused','cloud','configured')}
        data.update({k:0 for k in ('uptime_ms','verified','pending','failed','bytes_per_second','quiet_seconds','last_backup_unix','error','percent','phase')})
        data.update(firmware='0.4.7-wake-dev',done=2**64-1,total=2**64-1)
        connection=MagicMock();reply=connection.getresponse.return_value;reply.status=200;reply.getheader.return_value='application/json';reply.read.return_value=json.dumps(data).encode()
        with patch.object(lan.http.client,'HTTPConnection',return_value=connection):
            result=lan.status('192.168.1.42');self.assertEqual(result['done'],str(2**64-1))
            reply.status=302
            with self.assertRaises(ValueError):lan.status('192.168.1.42')
        self.assertEqual(connection.request.call_count,2);self.assertEqual(connection.close.call_count,2)
    def test_stale_install_pause_cannot_disable_startup_after_a_restart(self):
        from cloudstick_desktop import startup
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);flag=root/'watcher-paused'
            flag.write_text(json.dumps({'pid':1234,'token':'previous-process'}))
            with patch.object(startup,'process_token',return_value='previous-process'):
                self.assertTrue(startup.paused(root))
            with patch.object(startup,'process_token',return_value='different-process'):
                self.assertFalse(startup.paused(root))
            self.assertFalse(flag.exists())
    def test_native_startup_files_are_opt_in_and_metadata_watcher_has_no_open(self):
        import sys
        if sys.platform == 'win32':self.skipTest('Linux autostart file fixture; Windows packaged installer tests run separately')
        from cloudstick_desktop import startup
        with tempfile.TemporaryDirectory() as directory,patch.object(startup,'executable',return_value='/synthetic/Cloud Stick/cloud-stick'),patch.object(startup,'location',return_value=Path(directory)/'app.desktop'):
            self.assertFalse(startup.enabled());startup.configure(True);self.assertTrue(startup.enabled())
            self.assertIn(' --watch',startup.location().read_text());startup.configure(False);self.assertFalse(startup.enabled())


class HotplugIdentityTests(unittest.IsolatedAsyncioTestCase):
    async def test_watch_reports_an_opaque_change_when_same_port_is_replaced(self):
        from cloudstick_desktop.transports import DesktopTransport
        from test_transports import USB, BLE
        usb=USB();transport=DesktopTransport(lambda event:None,usb=usb,ble=BLE())
        first=await transport.dispatch({'op':'usb_watch_ports'})
        same=await transport.dispatch({'op':'usb_watch_ports'})
        self.assertEqual(first,same);self.assertEqual(set(first[0]),{'port','label','token'})
        self.assertNotIn('unit',json.dumps(first));self.assertNotIn('location',json.dumps(first))
        usb.listed['COM3']=(0x303a,0x1001,'replacement','replacement-location')
        new=await transport.dispatch({'op':'usb_watch_ports'})
        self.assertNotEqual(first[0]['token'],new[0]['token'])
        self.assertEqual(usb.calls,[])
        await transport.dispatch({'op':'disconnect'})
