package org.cloudstick.viewer;

import android.annotation.SuppressLint;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.ParcelUuid;
import java.util.*;

/** Thin native SDK adapter; session policy, admission and deadlines are separate. */
@SuppressLint("MissingPermission")
final class AndroidBleDriver implements GattSession.Driver {
    private final Context context; private final Handler handler; private final BluetoothAdapter adapter;
    GattSession session; private BluetoothGatt gatt; private ScanCallback scan;
    AndroidBleDriver(Context context, Handler handler) {
        this.context=context; this.handler=handler;
        BluetoothManager manager=context.getSystemService(BluetoothManager.class);
        adapter=manager==null?null:manager.getAdapter();
    }
    private BluetoothAdapter available() {
        if(adapter==null || !adapter.isEnabled())throw new IllegalStateException("bluetooth_unavailable");
        return adapter;
    }
    public void scan(long epoch) {
        BluetoothLeScanner scanner=available().getBluetoothLeScanner();
        if(scanner==null)throw new IllegalStateException("bluetooth_unavailable");
        scan=new ScanCallback() {
            @Override public void onScanResult(int type,ScanResult result) { handler.post(()->{
                if(scan!=this)return;
                try{
                    ScanRecord record=result.getScanRecord();
                    if(record==null)return;
                    List<ParcelUuid> services=record.getServiceUuids();
                    session.found(epoch,result.getDevice().getAddress(),record.getDeviceName(),
                        services!=null && services.contains(ParcelUuid.fromString(GattSession.SERVICE)));
                }catch(RuntimeException e){if(scan==this)session.failed(epoch,"bluetooth_scan_failed");}
            }); }
            @Override public void onScanFailed(int code) {handler.post(()->{if(scan==this)session.failed(epoch,"bluetooth_scan_failed");});}
        };
        scanner.startScan(List.of(new ScanFilter.Builder().setServiceUuid(ParcelUuid.fromString(GattSession.SERVICE)).build()),
            new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scan);
    }
    public void stopScan() {
        ScanCallback previous=scan;scan=null;
        if(previous!=null && adapter!=null) {
            BluetoothLeScanner scanner=adapter.getBluetoothLeScanner(); if(scanner!=null)scanner.stopScan(previous);
        }
    }
    public void connect(String device,long epoch) {
        if(gatt!=null)throw new IllegalStateException("disconnect_first");
        BluetoothDevice target=available().getRemoteDevice(device);
        BluetoothGattCallback callback=new BluetoothGattCallback() {
            private void deliver(BluetoothGatt source,Runnable action) { handler.post(()->{
                if(source!=gatt || epoch!=session.generation()) {try{source.close();}catch(RuntimeException ignored){}return;}
                try{action.run();}catch(RuntimeException e){session.failed(epoch,"bluetooth_unavailable");}
            }); }
            @Override public void onConnectionStateChange(BluetoothGatt source,int status,int state) {
                deliver(source,()->{
                    if(status!=BluetoothGatt.GATT_SUCCESS || state==BluetoothProfile.STATE_DISCONNECTED)session.disconnected(epoch);
                    else if(state==BluetoothProfile.STATE_CONNECTED)session.connected(epoch,true);
                });
            }
            @Override public void onMtuChanged(BluetoothGatt source,int mtu,int status) {deliver(source,()->session.mtu(epoch,mtu,status==BluetoothGatt.GATT_SUCCESS));}
            @Override public void onServicesDiscovered(BluetoothGatt source,int status) {deliver(source,()->{
                Set<String> valid=new HashSet<>();
                BluetoothGattService service=source.getService(UUID.fromString(GattSession.SERVICE));
                if(service!=null)for(BluetoothGattCharacteristic c:service.getCharacteristics()){
                    int required=BluetoothGattCharacteristic.PROPERTY_READ|BluetoothGattCharacteristic.PROPERTY_WRITE;
                    // No CCCD subscription is needed: reviewed protocomm is response-write/read.
                    if((c.getProperties()&required)==required)valid.add(c.getUuid().toString());
                }
                session.services(epoch,valid,status==BluetoothGatt.GATT_SUCCESS);
            }); }
            @Override public void onCharacteristicRead(BluetoothGatt source,BluetoothGattCharacteristic c,byte[] value,int status) {
                byte[] owned=value==null?null:value.clone();
                deliver(source,()->session.readDone(epoch,c.getUuid().toString(),owned,status==BluetoothGatt.GATT_SUCCESS));
            }
            @SuppressWarnings("deprecation")
            @Override public void onCharacteristicRead(BluetoothGatt source,BluetoothGattCharacteristic c,int status) {
                if(Build.VERSION.SDK_INT<33){byte[] owned=c.getValue()==null?null:c.getValue().clone();
                    deliver(source,()->session.readDone(epoch,c.getUuid().toString(),owned,status==BluetoothGatt.GATT_SUCCESS));}
            }
            @Override public void onCharacteristicWrite(BluetoothGatt source,BluetoothGattCharacteristic c,int status) {
                deliver(source,()->session.writeDone(epoch,c.getUuid().toString(),status==BluetoothGatt.GATT_SUCCESS));
            }
        };
        gatt=target.connectGatt(context,false,callback,BluetoothDevice.TRANSPORT_LE);
        if(gatt==null)throw new IllegalStateException("bluetooth_connect_failed");
    }
    public boolean requestMtu(int mtu) {return gatt!=null && gatt.requestMtu(mtu);}
    public boolean discover() {return gatt!=null && gatt.discoverServices();}
    private BluetoothGattCharacteristic characteristic(String uuid) {
        if(gatt==null)return null;
        BluetoothGattService service=gatt.getService(UUID.fromString(GattSession.SERVICE));
        return service==null?null:service.getCharacteristic(UUID.fromString(uuid));
    }
    public boolean read(String uuid) {BluetoothGattCharacteristic c=characteristic(uuid);return c!=null && gatt.readCharacteristic(c);}
    @SuppressWarnings("deprecation")
    public boolean write(String uuid,byte[] value) {
        BluetoothGattCharacteristic c=characteristic(uuid); if(c==null)return false;
        if(Build.VERSION.SDK_INT>=33)return gatt.writeCharacteristic(c,value,BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==BluetoothStatusCodes.SUCCESS;
        c.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);c.setValue(value);return gatt.writeCharacteristic(c);
    }
    public void close() {
        BluetoothGatt previous=gatt;gatt=null;
        if(previous!=null){try{previous.disconnect();}finally{previous.close();}}
    }
}
