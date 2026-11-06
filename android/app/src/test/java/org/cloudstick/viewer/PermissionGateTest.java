package org.cloudstick.viewer;
import org.junit.Test;
import static org.junit.Assert.*;

public class PermissionGateTest {
    @Test public void grantBeforeResumeWaitsForFreshForeground(){PermissionGate gate=new PermissionGate();GattSessionTest.Result result=new GattSessionTest.Result();assertTrue(gate.begin(result));gate.result(true);assertEquals(0,result.calls);gate.foreground(true);assertEquals(1,result.calls);assertNull(result.error);gate.foreground(false);gate.foreground(true);assertEquals(1,result.calls);}
    @Test public void grantAfterResumeAndDenialAreBoundedSingleReplies(){PermissionGate gate=new PermissionGate();gate.foreground(true);GattSessionTest.Result result=new GattSessionTest.Result();gate.begin(result);gate.result(true);assertEquals(1,result.calls);GattSessionTest.Result denied=new GattSessionTest.Result();gate.begin(denied);gate.result(false);assertEquals("bluetooth_permission_denied",denied.error);}
    @Test public void cancellationAndTimeoutCannotReviveOldPermission(){PermissionGate gate=new PermissionGate();GattSessionTest.Result result=new GattSessionTest.Result(),overlap=new GattSessionTest.Result();gate.begin(result);assertFalse(gate.begin(overlap));assertEquals("permission_pending",overlap.error);gate.cancel("permission_timeout");gate.result(true);gate.foreground(true);assertEquals(1,result.calls);assertEquals("permission_timeout",result.error);}
}
