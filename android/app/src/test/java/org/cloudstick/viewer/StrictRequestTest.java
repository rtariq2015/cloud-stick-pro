package org.cloudstick.viewer;
import org.junit.Test;
import static org.junit.Assert.*;
import java.util.Map;

public class StrictRequestTest {
    @Test public void dashboardOperationsAreTypedAndCannotChoosePathsOrSendCredentials(){
        for(String op:new String[]{"dashboard_address","dashboard_status","dashboard_cancel"})assertEquals(op,StrictRequest.validated("{\"id\":1,\"op\":\""+op+"\"}").get("op"));
        assertEquals("",StrictRequest.validated("{\"id\":1,\"op\":\"dashboard_save\",\"address\":\"\"}").get("address"));
        for(String raw:new String[]{"{\"id\":1,\"op\":\"dashboard_status\",\"address\":\"192.168.1.1\"}","{\"id\":1,\"op\":\"dashboard_save\",\"address\":\"8.8.8.8\"}","{\"id\":1,\"op\":\"dashboard_save\",\"address\":\"192.168.1.1\",\"password\":\"bad\"}"})assertThrows(IllegalArgumentException.class,()->StrictRequest.validated(raw));
    }
    @Test public void semanticRejectionKeepsTheValidRequestIdButNeverAdmitsTheRequest(){
        for(String raw:new String[]{"{\"id\":7,\"op\":\"dashboard_save\",\"address\":\"8.8.8.8\"}","{\"id\":7,\"op\":\"unsupported\"}","{\"id\":7,\"op\":\"dashboard_status\",\"password\":\"public-test\"}"}){
            assertEquals(7,StrictRequest.requestId(raw));assertThrows(IllegalArgumentException.class,()->StrictRequest.validated(raw));
        }
        for(String raw:new String[]{"{\"id\":7,\"id\":8,\"op\":\"dashboard_status\"}","{\"id\":0,\"op\":\"dashboard_status\"}","{\"id\":\"7\",\"op\":\"dashboard_status\"}","{}"})assertThrows(IllegalArgumentException.class,()->StrictRequest.requestId(raw));
    }
    @Test public void canonicalRequestsAccepted(){assertEquals(Map.of("id",1,"op","discover"),StrictRequest.validated("{\"id\":1,\"op\":\"discover\"}"));assertEquals("aa".repeat(512),StrictRequest.validated("{\"id\":2,\"op\":\"write\",\"uuid\":\""+GattSessionTest.UUID+"\",\"hex\":\""+"aa".repeat(512)+"\"}").get("hex"));}
    @Test public void malformedJsonAndAmbiguousTypesRejected(){for(String value:new String[]{"{\"id\":true,\"op\":\"discover\"}","{\"id\":1.0,\"op\":\"discover\"}","{\"id\":01,\"op\":\"discover\"}","{\"id\":2147483648,\"op\":\"discover\"}","{\"id\":1,\"op\":\"discover\",\"op\":\"disconnect\"}","{id:1,op:discover}","{\"id\":1,\"op\":\"discover\",}","{\"id\":1,\"op\":\"discover\"}null","{\"id\":1,\"op\":\"disc\novery\"}","[]","null","{\"id\":1,\"op\":[]}"})assertThrows(value,IllegalArgumentException.class,()->StrictRequest.validated(value));}
    @Test public void extraFieldsUnsupportedOpsAndIdsRejected(){for(String value:new String[]{"{\"id\":1,\"op\":\"discover\",\"password\":\"private\"}","{\"id\":0,\"op\":\"disconnect\"}","{\"id\":1,\"op\":\"flash\"}","{\"id\":1,\"op\":\"connect\",\"device_id\":\"aa:bb:cc:dd:ee:01\"}","{\"id\":1,\"op\":\"read\",\"uuid\":\"other\"}","{\"id\":1,\"op\":\"read\",\"uuid\":1}"})assertThrows(value,IllegalArgumentException.class,()->StrictRequest.validated(value));}
    @Test public void payloadSizeEncodingAndControlBounds(){for(String hex:new String[]{"","A0","zz","aa".repeat(513),"ab\ncd"}){String value="{\"id\":1,\"op\":\"write\",\"uuid\":\""+GattSessionTest.UUID+"\",\"hex\":\""+hex+"\"}";assertThrows(IllegalArgumentException.class,()->StrictRequest.validated(value));}assertThrows(IllegalArgumentException.class,()->StrictRequest.validated(" ".repeat(4097)));}
}
