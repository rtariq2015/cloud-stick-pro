"""Source-only GATT adapter contracts: BlueZ/hardware are deliberately mocked."""
import importlib.util,unittest
from pathlib import Path
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('native_bridge',ROOT/'tools/native_ble/bluez_bridge.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class AdapterTest(unittest.TestCase):
    def setUp(self):
        b=module.Bridge.__new__(module.Bridge);self.b=b
        b.address='02:11:22:33:44:55';b.path='/org/bluez/hci0/dev_02_11_22_33_44_55'
        self.values={'Address':b.address,'Connected':False,'ServicesResolved':False}
        b.props=SimpleNamespace(Get=lambda interface,key:self.values[key])
        def connect(**kw):self.values.update(Connected=True,ServicesResolved=True)
        def disconnect(**kw):self.values.update(Connected=False,ServicesResolved=False)
        b.control=SimpleNamespace(Connect=connect,Disconnect=disconnect)
        self.bytes=b'public-synthetic';self.writes=[]
        self.char=SimpleNamespace(ReadValue=lambda *a,**k:self.bytes,WriteValue=lambda value,options,**kw:self.writes.append((bytes(value),options)))
        b.dbus=SimpleNamespace(Interface=lambda obj,name:obj,Array=lambda data,**kw:data,
                              Dictionary=lambda data,**kw:data,String=lambda data,**kw:data)
        b.bus=SimpleNamespace(get_object=lambda owner,path:self.char)
        service=b.path+'/service0001'
        self.objects={service:{'org.bluez.GattService1':{'Device':b.path,'UUID':module.SERVICE}}}
        for i,uuid in enumerate(module.CHARACTERISTICS):
            self.objects[service+'/char'+str(i)]={'org.bluez.GattCharacteristic1':{'Service':service,'UUID':uuid}}
        b.objects=lambda:self.objects;b.characteristics={};self.uuid=next(iter(module.CHARACTERISTICS))
    def connected(self):self.assertEqual(self.b.call({'op':'connect'}),{'connected':True})
    def test_exact_discovery_and_reconnection(self):
        for _ in range(2):
            self.connected();self.assertEqual(set(self.b.characteristics),module.CHARACTERISTICS)
            self.b.call({'op':'write','uuid':self.uuid,'hex':'2d2d2d'})
            self.assertEqual(self.writes[-1],(b'---',{'type':'request'}))
            self.assertEqual(self.b.call({'op':'read','uuid':self.uuid}),{'hex':self.bytes.hex()})
            self.assertEqual(self.b.call({'op':'disconnect'}),{'connected':False})
            self.assertFalse(self.b.characteristics)
    def test_wrong_device(self):
        self.values['Address']='00:11:22:33:44:55'
        with self.assertRaises(AssertionError):self.b.call({'op':'connect'})
    def test_wrong_service_and_missing_characteristic(self):
        for field in ['UUID','Device']:
            with self.subTest(field=field):
                obj=next(iter(self.objects.values()))['org.bluez.GattService1'];old=obj[field];obj[field]='wrong'
                with self.assertRaises(AssertionError):self.b.call({'op':'connect'})
                obj[field]=old
        self.objects.pop(next(k for k in self.objects if '/char' in k))
        with self.assertRaises(AssertionError):self.b.call({'op':'connect'})
    def test_closed_link_and_unknown_uuid(self):
        self.connected();self.values['Connected']=False
        with self.assertRaises(module.DisconnectedError):self.b.call({'op':'read','uuid':self.uuid})
        self.values['Connected']=True
        with self.assertRaises(ValueError):self.b.call({'op':'read','uuid':'unknown'})
        with self.assertRaises(ValueError):self.b.call({'op':'pair'})
    def test_gatt_length_bounds(self):
        self.connected()
        for value in ['', 'a', 'gg', 'aa '*3, 'aa'*513]:
            with self.subTest(value_length=len(value)):
                with self.assertRaises(ValueError):self.b.call({'op':'write','uuid':self.uuid,'hex':value})
        self.b.call({'op':'write','uuid':self.uuid,'hex':'aa'*512})
        self.bytes=b'a'*513
        with self.assertRaises(ValueError):self.b.call({'op':'read','uuid':self.uuid})

if __name__=='__main__':unittest.main()
