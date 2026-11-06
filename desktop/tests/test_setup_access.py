"""Synthetic protected files/USB metadata only; never open the real setup config."""
import ast
import contextlib
import io
import json
import os
from pathlib import Path
import stat
import tempfile
import threading
import unittest
from types import SimpleNamespace as NS
from unittest.mock import patch

from cloudstick_desktop.protocol import TransportError,validate_request
from cloudstick_desktop.setup_access import SetupAccess
from cloudstick_desktop.transports import DesktopTransport
from test_transports import BLE,USB,SERVICE

KEY="SYNTHETIC-ONLY-KEY-0123456789abc"
DEVICE="AA:BB:CC:DD:EE:10"
FINGERPRINT=(12346,4097,"SYNTHETIC-USB", "synthetic-location")


class Files:
    def __init__(self):
        self.temporary=tempfile.TemporaryDirectory(prefix="csp-access-synthetic-")
        self.root=Path(self.temporary.name);self.root.chmod(0o700)
        self.directory=self.root/".local/share/cloud-stick-desktop"
        self.directory.mkdir(parents=True,mode=0o700);self.directory.chmod(0o700)
        self.key=self.root/"Downloads/fixture-key.txt";self.key.parent.mkdir(mode=0o700)
        self.key.write_bytes(KEY.encode()+b"\n");self.key.chmod(0o600)
        self.config=self.directory/"setup-access-v1.json"
        self.value={"version":1,"usb":{"vid":12346,"pid":4097,"serial_number":"SYNTHETIC-USB"},
                    "device_id":DEVICE,"credential_file":str(self.key)}
        self.save();self.access=SetupAccess(trusted_root=self.root)

    def save(self):
        self.config.write_text(json.dumps(self.value));self.config.chmod(0o600)

    def close(self):self.temporary.cleanup()


class ProtectedAccessTests(unittest.TestCase):
    def setUp(self):self.files=Files()
    def tearDown(self):self.files.close()
    def denied(self,code=None):
        with self.assertRaises(TransportError) as caught:self.files.access.load(FINGERPRINT)
        if code:self.assertEqual(caught.exception.code,code)
        self.assertNotIn(KEY,str(caught.exception));self.assertNotIn(str(self.files.key),str(caught.exception))
        self.assertFalse(self.files.access.available(FINGERPRINT))

    def test_exact_key_and_target_with_supported_line_endings(self):
        self.assertEqual(len(KEY),32)
        for ending in (b"",b"\n",b"\r\n",b"\r"):
            with self.subTest(ending=ending):
                self.files.key.write_bytes(KEY.encode()+ending)
                self.assertEqual(self.files.access.load(FINGERPRINT),{"key":KEY,"device_id":DEVICE})
                self.assertIs(self.files.access.available(FINGERPRINT),True)

    def test_status_and_exceptions_never_print_or_return_key(self):
        output,error=io.StringIO(),io.StringIO()
        with contextlib.redirect_stdout(output),contextlib.redirect_stderr(error):
            result={"available":self.files.access.available(FINGERPRINT)}
            self.files.key.write_bytes(b"bad")
            self.denied("setup_access_invalid_key")
        self.assertEqual(result,{"available":True})
        self.assertNotIn(KEY,json.dumps(result));self.assertEqual(output.getvalue()+error.getvalue(),"")

    def test_os_account_home_ignores_environment(self):
        with patch("pwd.getpwuid",return_value=NS(pw_dir="/synthetic-account-home")), \
             patch.dict(os.environ,{"HOME":"/untrusted-home","XDG_DATA_HOME":"/untrusted-data"}):
            access=SetupAccess()
        self.assertEqual(str(access._config),"/synthetic-account-home/.local/share/cloud-stick-desktop/setup-access-v1.json")

    def test_unsupported_platform_fails_closed_without_filesystem_open(self):
        with patch("cloudstick_desktop.setup_access.sys.platform","win32"),patch("cloudstick_desktop.setup_access.os.open") as opened:
            access=SetupAccess();self.assertFalse(access.available(FINGERPRINT));opened.assert_not_called()

    def test_strict_schema_types_and_canonical_identity(self):
        mutations=[("version",True),("version",2),("device_id",DEVICE.lower()),("device_id","not-an-address"),
                   ("credential_file","relative/key"),("extra","no-renderer-path")]
        for key,value in mutations:
            with self.subTest(key=key,value=value):
                original=self.files.value.copy();self.files.value[key]=value;self.files.save();self.denied()
                self.files.value=original
        for key,value in [("vid",True),("pid",False),("vid",12347),("serial_number",""),("serial_number","bad\nserial"),("extra",1)]:
            with self.subTest(usb_key=key):
                original=self.files.value['usb'].copy();self.files.value['usb'][key]=value;self.files.save();self.denied()
                self.files.value['usb']=original

    def test_duplicate_json_fields_at_both_levels(self):
        source=json.dumps(self.files.value)
        for text in [source[:-1]+',"version":1}',source.replace('"vid": 12346','"vid":12346,"vid":12346')]:
            self.files.config.write_text(text);self.denied()

    def test_wrong_usb_is_rejected_before_key_read(self):
        self.files.key.unlink()
        for fingerprint in [(12347,4097,"SYNTHETIC-USB","x"),(12346,4097,"WRONG","x"),tuple(),list(FINGERPRINT)]:
            with self.subTest(fingerprint=fingerprint),self.assertRaises(TransportError) as caught:
                self.files.access.load(fingerprint)
            self.assertEqual(caught.exception.code,"setup_access_device_mismatch")

    def test_config_and_key_permissions_revalidated(self):
        for target,unsafe in [(self.files.directory,0o755),(self.files.config,0o644),(self.files.key,0o644),
                              (self.files.key.parent,0o777),(self.files.root/".local",0o775)]:
            with self.subTest(target=target.name):
                original=stat.S_IMODE(target.stat().st_mode);target.chmod(unsafe);self.denied();target.chmod(original)
                self.assertTrue(self.files.access.available(FINGERPRINT))

    def test_sticky_writable_directory_not_trusted(self):
        self.files.key.parent.chmod(0o1777);self.denied()

    def test_other_owner_refused(self):
        original=os.fstat
        def changed(descriptor):
            value=original(descriptor)
            if stat.S_ISREG(value.st_mode):
                fields={name:getattr(value,name) for name in ['st_dev','st_ino','st_mode','st_uid','st_gid','st_size','st_mtime_ns','st_ctime_ns','st_nlink']}
                fields['st_uid']=os.getuid()+1
                return NS(**fields)
            return value
        with patch("cloudstick_desktop.setup_access.os.fstat",side_effect=changed):self.denied()

    def test_symlink_file_config_or_ancestor_refused(self):
        for target in [self.files.key,self.files.config,self.files.key.parent]:
            with self.subTest(target=target.name):
                moved=target.with_name(target.name+"-moved");target.rename(moved);target.symlink_to(moved,target_is_directory=moved.is_dir())
                self.denied();target.unlink();moved.rename(target)

    def test_hardlinked_credentials_refused(self):
        alias=self.files.key.with_name("alias");os.link(self.files.key,alias);self.denied();alias.unlink()

    def test_size_bounds_and_invalid_key_bytes(self):
        for payload in [b"x"*31,b"x"*33,b"x"*257,b"x"*31+b"\x00",b"x"*31+b"\x7f",b"x"*31+b"\xff",
                        b"x"*31+b" ",b"x"*31+b"!",b"x"*31+b"/",b"x"*31+b"=",KEY.encode()+b"\n\n"]:
            self.files.key.write_bytes(payload);self.denied()
        self.files.save();self.files.config.write_bytes(b" "*4097);self.denied()

    def test_fifo_key_refused_without_blocking(self):
        self.files.key.unlink();os.mkfifo(self.files.key,0o600);self.denied()

    def test_same_size_change_during_read_is_rejected(self):
        original=os.read;inode=self.files.key.stat().st_ino;changed=False
        def racing(descriptor,size):
            nonlocal changed
            value=original(descriptor,size)
            if not changed and os.fstat(descriptor).st_ino==inode:
                changed=True;self.files.key.write_bytes(b"Z"*33)
            return value
        with patch("cloudstick_desktop.setup_access.os.read",side_effect=racing):self.denied()

    def test_replaced_path_during_config_read_is_rejected(self):
        original=os.read;inode=self.files.config.stat().st_ino;changed=False
        def racing(descriptor,size):
            nonlocal changed
            value=original(descriptor,size)
            if not changed and os.fstat(descriptor).st_ino==inode:
                changed=True;self.files.config.rename(self.files.config.with_suffix('.old'));self.files.save()
            return value
        with patch("cloudstick_desktop.setup_access.os.read",side_effect=racing),self.assertRaises(TransportError):
            self.files.access.load(FINGERPRINT)
        self.assertTrue(self.files.access.available(FINGERPRINT))

    def test_forget_removes_only_validated_config_and_fsyncs(self):
        before=self.files.key.read_bytes()
        with patch("cloudstick_desktop.setup_access.os.fsync",wraps=os.fsync) as sync:
            self.assertEqual(self.files.access.forget(FINGERPRINT),{"forgotten":True});sync.assert_called_once()
        self.assertFalse(self.files.config.exists());self.assertEqual(self.files.key.read_bytes(),before)
        self.assertFalse(self.files.access.available(FINGERPRINT))

    def test_forget_wrong_device_or_unsafe_config_preserves_both_files(self):
        for wrong in [True,False]:
            if not wrong:self.files.config.chmod(0o644)
            with self.assertRaises(TransportError):self.files.access.forget((12346,4097,"WRONG","x") if wrong else FINGERPRINT)
            self.assertTrue(self.files.config.exists());self.assertTrue(self.files.key.exists())

    def test_forget_rejects_config_changed_after_key_validation(self):
        original=self.files.access._read
        def racing(path,bound,*args,**kwargs):
            value=original(path,bound,*args,**kwargs)
            if path==self.files.key:self.files.value['device_id']="AA:BB:CC:DD:EE:11";self.files.save()
            return value
        with patch.object(self.files.access,"_read",side_effect=racing),self.assertRaises(TransportError):
            self.files.access.forget(FINGERPRINT)
        self.assertTrue(self.files.config.exists());self.assertTrue(self.files.key.exists())

    def test_renderer_cannot_choose_config_or_credential_path(self):
        for op in ['usb_setup_access_status','usb_setup_access','usb_forget_setup_access']:
            self.assertEqual(validate_request({'op':op,'port':'COM3'}),op)
            for extra in [{'credential_file':'/arbitrary'},{'key':KEY},{'device_id':DEVICE}]:
                with self.assertRaises(TransportError):validate_request({'op':op,'port':'COM3',**extra})

    def test_main_report_redactor_and_bridge_allowlist(self):
        path=Path(__file__).resolve().parents[1]/'cloudstick_desktop/main.py';tree=ast.parse(path.read_text())
        function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='reportable_response')
        namespace={'json':json};exec(compile(ast.Module(body=[function],type_ignores=[]),'report-helper','exec'),namespace)
        result=namespace['reportable_response'](json.dumps({'id':1,'ok':True,'result':{'key':KEY,'device_id':DEVICE}}))
        self.assertNotIn(KEY,json.dumps(result));self.assertNotIn(DEVICE,json.dumps(result))
        self.assertEqual(result['error']['code'],'sensitive_response_omitted')
        literals=[n.value for n in ast.walk(tree) if isinstance(n,ast.Constant) and isinstance(n.value,str)]
        for op in ['usb_setup_access_status','usb_setup_access','usb_forget_setup_access']:self.assertIn(op,literals)


class BootstrapTransportTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.files=Files();self.ble=BLE();self.usb=USB();self.events=[]
        self.ble.device.address=DEVICE
        self.usb.listed={'COM3':FINGERPRINT}
        self.transport=DesktopTransport(self.events.append,ble=self.ble,usb=self.usb,setup_access=self.files.access,
                                        operation_timeout=.05,cleanup_timeout=.08)
        await self.transport.dispatch({'op':'usb_ports'})

    async def asyncTearDown(self):
        await self.transport.dispatch({'op':'disconnect'});self.files.close()

    async def test_status_boolean_and_explicit_key_only_no_serial_ble(self):
        self.assertEqual(await self.transport.dispatch({'op':'usb_setup_access_status','port':'COM3'}),{'available':True})
        self.assertIsNone(self.transport._setup_device_id)
        self.assertEqual(await self.transport.dispatch({'op':'usb_setup_access','port':'COM3'}),{'key':KEY,'device_id':DEVICE})
        self.assertEqual(self.usb.calls,[]);self.assertEqual(self.ble.clients,[]);self.assertEqual(self.events,[])

    async def test_unselected_and_reassigned_port_refused(self):
        with self.assertRaises(TransportError):await self.transport.dispatch({'op':'usb_setup_access','port':'COM4'})
        self.usb.listed['COM3']=(12346,4097,'REPLACED','x')
        with self.assertRaises(TransportError) as caught:await self.transport.dispatch({'op':'usb_setup_access','port':'COM3'})
        self.assertEqual(caught.exception.code,'usb_device_changed');self.assertIsNone(self.transport._setup_device_id)

    async def test_usb_change_during_key_read_returns_no_key_or_pin(self):
        original=self.files.access.load
        def changed(fingerprint):
            value=original(fingerprint);self.usb.listed['COM3']=(12346,4097,'REPLACED','x');return value
        with patch.object(self.files.access,'load',side_effect=changed),self.assertRaises(TransportError):
            await self.transport.dispatch({'op':'usb_setup_access','port':'COM3'})
        self.assertIsNone(self.transport._setup_device_id)

    async def test_target_pin_survives_window_and_discovery_rejects_other_device(self):
        await self.transport.dispatch({'op':'usb_setup_access','port':'COM3'})
        await self.transport.dispatch({'op':'usb_open_setup','port':'COM3'})
        other='AA:BB:CC:DD:EE:11'
        self.ble.rows[other]=(NS(address=other,name='CSP-W2-OTHER'),NS(service_uuids=[SERVICE],local_name=None))
        await self.transport.dispatch({'op':'discover'})
        self.assertEqual(self.transport._setup_device_id,DEVICE)
        with self.assertRaises(TransportError) as caught:await self.transport.dispatch({'op':'connect','device_id':other})
        self.assertEqual(caught.exception.code,'setup_access_device_mismatch');self.assertEqual(self.ble.clients,[])
        await self.transport.dispatch({'op':'connect','device_id':DEVICE})
        self.assertIsNone(self.transport._setup_device_id)
        self.assertEqual(self.usb.calls[0][2],'BENCH_WINDOW')

    async def test_disconnect_clears_pending_pin(self):
        await self.transport.dispatch({'op':'usb_setup_access','port':'COM3'})
        await self.transport.dispatch({'op':'disconnect'});self.assertIsNone(self.transport._setup_device_id)

    async def test_all_bootstrap_operations_refuse_active_ble(self):
        await self.transport.dispatch({'op':'discover'});await self.transport.dispatch({'op':'connect','device_id':DEVICE})
        for op in ['usb_setup_access_status','usb_setup_access','usb_forget_setup_access']:
            with self.assertRaises(TransportError) as caught:await self.transport.dispatch({'op':op,'port':'COM3'})
            self.assertEqual(caught.exception.code,'transport_busy')
        self.assertTrue(self.files.config.exists())

    async def test_retirement_removes_config_only_after_drained_connection(self):
        await self.transport.dispatch({'op':'usb_setup_access','port':'COM3'})
        await self.transport.dispatch({'op':'disconnect'})
        self.assertEqual(await self.transport.dispatch({'op':'usb_forget_setup_access','port':'COM3'}),{'forgotten':True})
        self.assertTrue(self.files.key.exists());self.assertFalse(self.files.config.exists())
        self.assertEqual(await self.transport.dispatch({'op':'usb_setup_access_status','port':'COM3'}),{'available':False})
        self.assertEqual(self.usb.calls,[])

    async def test_canceled_read_cannot_return_key_or_republish_pin(self):
        import asyncio
        started=threading.Event();release=threading.Event();original=self.files.access.load
        def slow(fingerprint):started.set();release.wait(1);return original(fingerprint)
        with patch.object(self.files.access,'load',side_effect=slow):
            task=asyncio.create_task(self.transport.dispatch({'op':'usb_setup_access','port':'COM3'}))
            while not started.is_set():await asyncio.sleep(.001)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):await task
            release.set();await self.transport.dispatch({'op':'disconnect'})
        self.assertIsNone(self.transport._setup_device_id);self.assertEqual(self.events,[])


if __name__=='__main__':unittest.main()
