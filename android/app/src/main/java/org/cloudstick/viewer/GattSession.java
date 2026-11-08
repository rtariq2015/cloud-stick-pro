package org.cloudstick.viewer;

import java.util.*;
import java.util.function.Consumer;

/** Single foreground GATT owner. Driver callbacks and requests run on one UI loop.
 * No automatic reconnection, pairing, settings commands, credentials or persistence. */
final class GattSession {
    static final String SERVICE = "021a9004-0382-4aea-bff4-6b3f1c5adfb4";
    static final Set<String> CHARACTERISTICS = Set.of(
        "021aff51-0382-4aea-bff4-6b3f1c5adfb4", "021aff52-0382-4aea-bff4-6b3f1c5adfb4",
        "021aff53-0382-4aea-bff4-6b3f1c5adfb4", "021aff54-0382-4aea-bff4-6b3f1c5adfb4");
    interface Reply { void ok(Map<String,Object> value); void error(String code); }
    interface Clock { void later(Runnable runnable, long milliseconds); }
    interface Driver {
        void scan(long generation); void stopScan();
        void connect(String device, long generation); boolean requestMtu(int mtu);
        boolean discover(); boolean read(String uuid); boolean write(String uuid, byte[] value);
        void close();
    }
    private final Driver driver; private final Clock clock; private final Consumer<Map<String,Object>> events;
    private final LinkedHashMap<String,String> devices = new LinkedHashMap<>();
    private boolean foreground, cleanupFailed; private long generation; private long operation;
    private String state="idle", pendingKind="", pendingUuid="", selectedDevice=""; private Reply pending;
    GattSession(Driver driver, Clock clock, Consumer<Map<String,Object>> events) { this.driver=driver; this.clock=clock; this.events=events; }
    long generation() { return generation; }
    String state() { return state; }
    String readyDevice(){return foreground&&!cleanupFailed&&state.equals("ready")&&pending==null?selectedDevice:null;}
    void foreground(boolean value) { foreground=value; if(!value) close("backgrounded"); }
    private void start(Reply reply, String kind, long ms) {
        pending=reply; pendingKind=kind; long token=++operation, epoch=generation;
        clock.later(()->{ if(token==operation && epoch==generation && pending!=null) close("bluetooth_timeout"); },ms);
    }
    private void ok(Map<String,Object> value) { Reply r=pending; clearPending(); if(r!=null)r.ok(value); }
    private void clearPending() { pending=null; pendingKind=""; pendingUuid=""; ++operation; }
    private boolean admitted(Reply r) {
        if(!foreground) {r.error("app_not_foreground"); return false;}
        if(cleanupFailed) {r.error("bluetooth_cleanup_failed"); return false;}
        if(pending!=null) {r.error("bluetooth_busy"); return false;}
        return true;
    }
    void scan(Reply r) {
        if(!admitted(r))return;
        if(!state.equals("idle")) {r.error("disconnect_first");return;}
        ++generation; devices.clear(); state="scanning"; start(r,"scan",12000);
        try { driver.scan(generation); }
        catch(RuntimeException e) {close("bluetooth_unavailable");return;}
        long epoch=generation;
        clock.later(()->{if(epoch==generation && state.equals("scanning")){
            try{driver.stopScan();}catch(RuntimeException e){close("bluetooth_scan_failed");return;} state="idle";
            List<Map<String,Object>> result=new ArrayList<>();
            devices.forEach((id,name)->result.add(Map.of("id",id,"name",name)));
            ok(Map.of("devices",result));
        }},6000);
    }
    void found(long epoch, String id, String name, boolean serviceAdvertised) {
        if(epoch!=generation || !state.equals("scanning") || !serviceAdvertised || devices.size()>=16)return;
        if(id==null || !id.matches("(?:[0-9A-F]{2}:){5}[0-9A-F]{2}") || name==null || !name.matches("CSP-W2-[A-Za-z0-9-]{1,32}"))return;
        devices.put(id,name);
    }
    void connect(String id, Reply r) {
        if(!admitted(r))return;
        if(!state.equals("idle")) {r.error("disconnect_first");return;}
        if(!devices.containsKey(id)) {r.error("device_not_discovered");return;}
        ++generation; state="connecting"; selectedDevice=id; start(r,"connect",15000);
        try {driver.connect(id,generation);} catch(RuntimeException e) {close("bluetooth_connect_failed");}
    }
    void connected(long epoch, boolean success) {
        if(epoch!=generation || !state.equals("connecting"))return;
        if(!success) {close("bluetooth_connect_failed");return;}
        state="mtu";
        try{if(!driver.requestMtu(517))close("bluetooth_mtu_failed");}catch(RuntimeException e){close("bluetooth_mtu_failed");}
    }
    void mtu(long epoch, int mtu, boolean success) {
        if(epoch!=generation || !state.equals("mtu"))return;
        // The current firmware prefers256. Android WRITE_TYPE_DEFAULT performs
        // standard ATT long writes; readCharacteristic assembles long reads.
        // Keep each canonical<=512-byte attribute intact, never fragment here.
        if(!success || mtu<23 || mtu>517) {close("bluetooth_mtu_failed");return;}
        state="services"; try{if(!driver.discover())close("bluetooth_service_failed");}catch(RuntimeException e){close("bluetooth_service_failed");}
    }
    void services(long epoch, Set<String> readWriteUuids, boolean success) {
        if(epoch!=generation || !state.equals("services"))return;
        if(!success || !readWriteUuids.containsAll(CHARACTERISTICS)) {close("bluetooth_service_mismatch");return;}
        state="ready"; devices.clear(); ok(Map.of());
    }
    void read(String uuid, Reply r) {
        if(!admitted(r))return;
        if(!state.equals("ready") || !CHARACTERISTICS.contains(uuid)) {r.error("bluetooth_not_ready");return;}
        start(r,"read",10000); pendingUuid=uuid;
        try{if(!driver.read(uuid))close("bluetooth_read_failed");}catch(RuntimeException e){close("bluetooth_read_failed");}
    }
    void write(String uuid, byte[] value, Reply r) {
        if(!admitted(r))return;
        if(!state.equals("ready") || !CHARACTERISTICS.contains(uuid)) {r.error("bluetooth_not_ready");return;}
        if(value==null || value.length<1 || value.length>512) {r.error("invalid_payload");return;}
        start(r,"write",10000); pendingUuid=uuid;
        try{if(!driver.write(uuid,value.clone()))close("bluetooth_write_failed");}catch(RuntimeException e){close("bluetooth_write_failed");}
    }
    void readDone(long epoch,String uuid,byte[] bytes,boolean success) {
        if(epoch!=generation || !pendingKind.equals("read") || !pendingUuid.equals(uuid))return;
        if(!success || bytes==null || bytes.length>512) {close("bluetooth_read_failed");return;}
        StringBuilder hex=new StringBuilder(bytes.length*2);
        for(byte b:bytes) hex.append(String.format(Locale.ROOT,"%02x",b&255));
        ok(Map.of("hex",hex.toString()));
    }
    void writeDone(long epoch,String uuid,boolean success) {
        if(epoch!=generation || !pendingKind.equals("write") || !pendingUuid.equals(uuid))return;
        if(!success)close("bluetooth_write_failed"); else ok(Map.of());
    }
    void disconnected(long epoch) {if(epoch==generation)close("bluetooth_disconnected");}
    void failed(long epoch,String code) {if(epoch==generation)close(code);}
    void disconnect(Reply r) {close("cancelled"); if(cleanupFailed)r.error("bluetooth_cleanup_failed");else r.ok(Map.of());}
    private void close(String code) {
        boolean hadSession=pending!=null || !state.equals("idle");
        ++generation; state="idle"; selectedDevice=""; devices.clear(); Reply r=pending; clearPending();
        try{driver.stopScan();}catch(RuntimeException ignored){cleanupFailed=true;}
        try{driver.close();}catch(RuntimeException ignored){cleanupFailed=true;}
        if(cleanupFailed)state="blocked";
        if(r!=null)r.error(code);
        if(hadSession)events.accept(Map.of("type","disconnected"));
    }
}
