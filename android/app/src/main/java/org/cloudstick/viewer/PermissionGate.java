package org.cloudstick.viewer;
import java.util.Map;

/** The OS permission result may precede onResume; release it only in foreground. */
final class PermissionGate {
    private boolean foreground; private Boolean decision; private GattSession.Reply pending;
    boolean begin(GattSession.Reply reply) {
        if(pending!=null){reply.error("permission_pending");return false;}
        pending=reply;decision=null;return true;
    }
    void foreground(boolean value){foreground=value;flush();}
    void result(boolean allowed){if(pending==null)return;decision=allowed;flush();}
    void cancel(String code){GattSession.Reply old=pending;pending=null;decision=null;if(old!=null)old.error(code);}
    private void flush(){
        if(!foreground || pending==null || decision==null)return;
        GattSession.Reply old=pending;boolean allowed=decision;pending=null;decision=null;
        if(allowed)old.ok(Map.of());else old.error("bluetooth_permission_denied");
    }
}
