"""Bounded native Linux GATT transport. No pairing, credentials or persistence.
One exact device; only the reviewed five service/characteristic UUIDs are allowed.
JSON pipes carry GATT bytes, never print them to diagnostic logs.
"""
import argparse,json,re,sys,time
SERVICE='021a9004-0382-4aea-bff4-6b3f1c5adfb4'
CHARACTERISTICS={f'021aff5{n}-0382-4aea-bff4-6b3f1c5adfb4' for n in (1,2,3,4)}

class DisconnectedError(ConnectionError):
    """Known peer reports its link closed; distinct from malformed requests."""

class Bridge:
    def __init__(self,address):
        import dbus
        self.dbus=dbus;self.bus=dbus.SystemBus();self.address=address.upper()
        self.path='/org/bluez/hci0/dev_'+self.address.replace(':','_')
        if self.path not in self.objects():
            adapter=dbus.Interface(self.bus.get_object('org.bluez','/org/bluez/hci0'),'org.bluez.Adapter1')
            adapter.SetDiscoveryFilter(dbus.Dictionary({'Transport':dbus.String('le',variant_level=1),
                'Pattern':dbus.String(self.address,variant_level=1)},signature='sv'))
            adapter.StartDiscovery()
            try:
                end=time.monotonic()+8
                while self.path not in self.objects():
                    if time.monotonic()>=end:raise TimeoutError('exact peer discovery')
                    time.sleep(.1)
            finally:adapter.StopDiscovery()
        self.device=self.bus.get_object('org.bluez',self.path)
        self.control=dbus.Interface(self.device,'org.bluez.Device1')
        self.props=dbus.Interface(self.device,'org.freedesktop.DBus.Properties')
        self.characteristics={}
    def objects(self):
        return self.dbus.Interface(self.bus.get_object('org.bluez','/'),'org.freedesktop.DBus.ObjectManager').GetManagedObjects()
    def connected(self):
        return bool(self.props.Get('org.bluez.Device1','Connected'))
    def call(self,request):
        op=request['op'];dbus=self.dbus
        if op=='connect':
            assert str(self.props.Get('org.bluez.Device1','Address')).upper()==self.address
            self.control.Connect(timeout=15)
            deadline=time.monotonic()+12
            while not self.props.Get('org.bluez.Device1','ServicesResolved'):
                if time.monotonic()>=deadline:raise TimeoutError('service discovery')
                time.sleep(.1)
            objects=self.objects();services={str(p) for p,v in objects.items() if
                str(v.get('org.bluez.GattService1',{}).get('Device',''))==self.path and
                str(v['org.bluez.GattService1'].get('UUID','')).lower()==SERVICE}
            assert len(services)==1
            for p,v in objects.items():
                c=v.get('org.bluez.GattCharacteristic1',{})
                if str(c.get('Service','')) in services and str(c.get('UUID','')).lower() in CHARACTERISTICS:
                    self.characteristics[str(c['UUID']).lower()]=str(p)
            assert set(self.characteristics)==CHARACTERISTICS
            return {'connected':True}
        if op=='disconnect':
            if self.connected():self.control.Disconnect(timeout=5)
            self.characteristics.clear();return {'connected':False}
        if op not in ('write','read'):raise ValueError('unsupported operation')
        uuid=request.get('uuid','').lower()
        if uuid not in self.characteristics or uuid not in CHARACTERISTICS:raise ValueError('unsupported characteristic')
        if not self.connected():raise DisconnectedError('peer disconnected')
        char=dbus.Interface(self.bus.get_object('org.bluez',self.characteristics[uuid]),'org.bluez.GattCharacteristic1')
        if op=='write':
            value=request.get('hex','')
            if not isinstance(value,str) or not re.fullmatch(r'(?:[0-9a-f]{2}){1,512}',value):raise ValueError('invalid GATT length')
            char.WriteValue(dbus.Array(bytes.fromhex(value),signature='y'),dbus.Dictionary({'type':dbus.String('request',variant_level=1)},signature='sv'),timeout=12)
            return {}
        value=bytes(char.ReadValue(dbus.Dictionary({},signature='sv'),timeout=12))
        if len(value)>512:raise ValueError('oversized response')
        return {'hex':value.hex()}

def main():
    p=argparse.ArgumentParser();p.add_argument('--address',required=True);args=p.parse_args()
    assert re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}',args.address)
    bridge=Bridge(args.address)
    try:
        for line in sys.stdin:
            if len(line)>2048:raise ValueError('oversized transport request')
            request=json.loads(line);identifier=request.get('id')
            if type(identifier)!=int or identifier<1:raise ValueError('invalid request identifier')
            try:answer={'id':identifier,'ok':True,**bridge.call(request)}
            except Exception as error:
                # Class only: exception text can contain private payloads.
                answer={'id':identifier,'ok':False,'error_type':type(error).__name__}
                name=error.get_dbus_name() if hasattr(error,'get_dbus_name') else ''
                if re.fullmatch(r'org\.(?:bluez|freedesktop)\.[A-Za-z0-9_.]{1,96}',name):answer['error_name']=name
            print(json.dumps(answer),flush=True)
    finally:
        try:bridge.call({'op':'disconnect'})
        except Exception:pass

if __name__=='__main__':main()
