package org.cloudstick.viewer;
import org.junit.*;
import static org.junit.Assert.*;
import java.util.*;

public class GattSessionTest {
    static final String DEVICE="AA:BB:CC:DD:EE:01", UUID="021aff51-0382-4aea-bff4-6b3f1c5adfb4";
    static class Result implements GattSession.Reply {
        int calls;Map<String,Object> value;String error;
        public void ok(Map<String,Object> value){calls++;this.value=value;}
        public void error(String error){calls++;this.error=error;}
    }
    static class Clock implements GattSession.Clock {
        record Task(long at,Runnable action){} long time;List<Task> tasks=new ArrayList<>();
        public void later(Runnable action,long ms){tasks.add(new Task(time+ms,action));}
        void advance(long ms){time+=ms;while(true){Task next=tasks.stream().filter(t->t.at<=time).min(Comparator.comparingLong(Task::at)).orElse(null);if(next==null)return;tasks.remove(next);next.action.run();}}
    }
    static class Driver implements GattSession.Driver {
        List<String> calls=new ArrayList<>();String fail="";byte[] written;
        void call(String name){calls.add(name);if(fail.equals(name))throw new SecurityException("private exception detail");}
        public void scan(long epoch){call("scan");} public void stopScan(){call("stop");}
        public void connect(String id,long epoch){call("connect");}public boolean requestMtu(int mtu){assertEquals(517,mtu);call("mtu");return true;}
        public boolean discover(){call("services");return true;}public boolean read(String uuid){call("read");return true;}
        public boolean write(String uuid,byte[] value){call("write");written=value;return true;}public void close(){call("close");}
    }
    Driver driver;Clock clock;GattSession session;List<Map<String,Object>> events;
    @Before public void init(){driver=new Driver();clock=new Clock();events=new ArrayList<>();session=new GattSession(driver,clock,events::add);session.foreground(true);}
    void discover(){Result r=new Result();session.scan(r);session.found(session.generation(),DEVICE,"CSP-W2-TEST",true);clock.advance(6000);assertEquals(1,r.calls);assertNull(r.error);}
    Result connecting(){discover();Result r=new Result();session.connect(DEVICE,r);return r;}
    void ready(int mtu){Result r=connecting();long epoch=session.generation();session.connected(epoch,true);session.mtu(epoch,mtu,true);session.services(epoch,GattSession.CHARACTERISTICS,true);assertEquals(Map.of(),r.value);assertEquals("ready",session.state());}
    @Test public void pairingIdentityExistsOnlyForAnIdleReadyLiveGeneration(){
        assertNull(session.readyDevice());ready(256);assertEquals(DEVICE,session.readyDevice());
        session.read(UUID,new Result());assertNull(session.readyDevice());session.readDone(session.generation(),UUID,new byte[]{1},true);assertEquals(DEVICE,session.readyDevice());
        session.disconnect(new Result());assertNull(session.readyDevice());ready(256);session.foreground(false);assertNull(session.readyDevice());
    }
    @Test public void discoveryRequiresForeground(){session.foreground(false);Result r=new Result();session.scan(r);assertEquals("app_not_foreground",r.error);assertFalse(driver.calls.contains("scan"));}
    @Test public void discoveredMatchingServiceAndNameOnly(){Result r=new Result();session.scan(r);long g=session.generation();session.found(g,DEVICE,"CSP-W2-TEST",false);session.found(g,DEVICE,"other",true);session.found(g,"not-an-id","CSP-W2-TEST",true);clock.advance(6000);assertEquals(List.of(),r.value.get("devices"));}
    @Test public void discoveryIsBounded(){Result r=new Result();session.scan(r);for(int i=0;i<30;i++)session.found(session.generation(),String.format("AA:BB:CC:DD:EE:%02X",i),"CSP-W2-T",true);clock.advance(6000);assertEquals(16,((List<?>)r.value.get("devices")).size());}
    @Test public void arbitraryConnectRejected(){Result r=new Result();session.connect(DEVICE,r);assertEquals("device_not_discovered",r.error);assertFalse(driver.calls.contains("connect"));}
    @Test public void oldDiscoveryCannotSeedNewSession(){session.scan(new Result());long old=session.generation();session.disconnect(new Result());session.scan(new Result());session.found(old,DEVICE,"CSP-W2-T",true);clock.advance(6000);Result r=new Result();session.connect(DEVICE,r);assertEquals("device_not_discovered",r.error);}
    @Test public void connectionWaitsForMtuAndAllFourCharacteristics(){Result r=connecting();long g=session.generation();session.connected(g,true);assertEquals(0,r.calls);session.mtu(g,517,true);assertEquals(0,r.calls);session.services(g,Set.of(UUID),true);assertEquals("bluetooth_service_mismatch",r.error);assertEquals("idle",session.state());}
    @Test public void preferred256UsesWholeLongAttribute(){ready(256);byte[] payload=new byte[512];Arrays.fill(payload,(byte)0xa5);Result write=new Result();session.write(UUID,payload,write);assertArrayEquals(payload,driver.written);assertEquals(512,driver.written.length);session.writeDone(session.generation(),UUID,true);Result read=new Result();session.read(UUID,read);session.readDone(session.generation(),UUID,payload,true);assertEquals("a5".repeat(512),read.value.get("hex"));assertEquals(1,write.calls);}
    @Test public void maximum517IsSupported(){ready(517);}
    @Test public void validMinimumUsesSdkLongAttributes(){ready(23);}
    @Test public void invalidMtuFailsClosed(){for(int value:new int[]{0,22,518}){init();Result r=connecting();long g=session.generation();session.connected(g,true);session.mtu(g,value,true);assertEquals("bluetooth_mtu_failed",r.error);assertFalse(driver.calls.contains("services"));}}
    @Test public void readCannotOvertakeWriteResponse(){ready(256);Result write=new Result(),read=new Result();session.write(UUID,new byte[]{1},write);session.read(UUID,read);assertEquals("bluetooth_busy",read.error);assertFalse(driver.calls.contains("read"));session.writeDone(session.generation(),UUID,true);session.read(UUID,new Result());assertTrue(driver.calls.contains("read"));}
    @Test public void writesAreCopiedAndBounded(){ready(256);for(int length:new int[]{0,513}){Result r=new Result();session.write(UUID,new byte[length],r);assertEquals("invalid_payload",r.error);}byte[] value={1};session.write(UUID,value,new Result());value[0]=2;assertEquals(1,driver.written[0]);}
    @Test public void wrongUuidNeverReachesSdk(){ready(256);Result r=new Result();session.write("021aff99",new byte[]{1},r);assertEquals("bluetooth_not_ready",r.error);assertFalse(driver.calls.contains("write"));}
    @Test public void timeoutClosesAndRejectsExactlyOnce(){Result r=connecting();long g=session.generation();clock.advance(15000);assertEquals(1,r.calls);assertEquals("bluetooth_timeout",r.error);session.connected(g,true);assertEquals(1,r.calls);assertEquals("idle",session.state());assertTrue(driver.calls.contains("close"));}
    @Test public void backgroundClosesAndRejectsLateRead(){ready(256);Result r=new Result();session.read(UUID,r);long old=session.generation();session.foreground(false);session.readDone(old,UUID,new byte[]{42},true);assertEquals(1,r.calls);assertEquals("backgrounded",r.error);assertNull(r.value);assertEquals(Map.of("type","disconnected"),events.get(0));}
    @Test public void cancellationClosesLateConnect(){Result r=connecting();long old=session.generation();session.disconnect(new Result());session.connected(old,true);assertEquals("cancelled",r.error);assertFalse(driver.calls.contains("mtu"));}
    @Test public void reconnectDoesNotAcceptOldResult(){ready(256);long old=session.generation();session.disconnect(new Result());ready(256);Result r=new Result();session.read(UUID,r);session.readDone(old,UUID,new byte[]{1},true);assertEquals(0,r.calls);session.readDone(session.generation(),UUID,new byte[]{2},true);assertEquals("02",r.value.get("hex"));}
    @Test public void unsuccessfulWriteStopsBeforeRead(){ready(256);Result r=new Result();session.write(UUID,new byte[]{1},r);session.writeDone(session.generation(),UUID,false);assertEquals("bluetooth_write_failed",r.error);assertEquals("idle",session.state());assertFalse(driver.calls.contains("read"));}
    @Test public void oversizedReadClosesWithoutReturningData(){ready(256);Result r=new Result();session.read(UUID,r);session.readDone(session.generation(),UUID,new byte[513],true);assertEquals("bluetooth_read_failed",r.error);assertNull(r.value);}
    @Test public void runtimeFailureDuringMtuCloses(){Result r=connecting();driver.fail="mtu";session.connected(session.generation(),true);assertEquals("bluetooth_mtu_failed",r.error);assertEquals("idle",session.state());}
    @Test public void runtimeFailureDuringServiceDiscoveryCloses(){Result r=connecting();long g=session.generation();session.connected(g,true);driver.fail="services";session.mtu(g,256,true);assertEquals("bluetooth_service_failed",r.error);}
    @Test public void runtimeFailureStoppingScanCloses(){Result r=new Result();session.scan(r);driver.fail="stop";clock.advance(6000);assertEquals("bluetooth_scan_failed",r.error);assertEquals("blocked",session.state());}
    @Test public void unknownCleanupFailureBlocksReuseEvenAfterForegroundCycle(){ready(256);driver.fail="close";Result r=new Result();session.disconnect(r);assertEquals("bluetooth_cleanup_failed",r.error);driver.fail="";session.foreground(false);session.foreground(true);Result next=new Result();session.scan(next);assertEquals("bluetooth_cleanup_failed",next.error);assertEquals("blocked",session.state());}
    @Test public void scanCallbackFailureClosesAndOldFailureCannotStopFreshDiscovery(){Result first=new Result();session.scan(first);long old=session.generation();session.failed(old,"bluetooth_scan_failed");assertEquals("bluetooth_scan_failed",first.error);Result fresh=new Result();session.scan(fresh);session.failed(old,"bluetooth_scan_failed");session.found(session.generation(),DEVICE,"CSP-W2-T",true);clock.advance(6000);assertNull(fresh.error);assertEquals(1,fresh.calls);}
    @Test public void idlePermissionPauseResumeDoesNotPublishStaleDisconnect(){session.foreground(false);session.foreground(true);session.disconnect(new Result());assertEquals(List.of(),events);Result fresh=new Result();session.scan(fresh);session.found(session.generation(),DEVICE,"CSP-W2-T",true);clock.advance(6000);assertNull(fresh.error);assertEquals(1,fresh.calls);assertTrue(events.isEmpty());}
}
