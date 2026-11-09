/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import org.junit.Test;
import static org.junit.Assert.*;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
public class AppUpdatesTest {
    @Test public void issuedPublicVectorVerifiesAndCannotBeMovedToAnotherPayload()throws Exception {
        byte[] payload="Verification vector only; this is not a release catalog.".getBytes(StandardCharsets.US_ASCII);
        byte[] signature=Base64.getDecoder().decode("MEUCIQD3eHdgVTzPu4ec8+DhIGLTCcpVhcibujto2vPCnD4DdAIgOH9HYAl7FZeseIF2ZyhUHz/iODPSbvtNq+70HGNCrQ0=");
        assertArrayEquals(payload,AppUpdates.verified(payload,signature));
        payload[0]^=1;assertThrows(java.security.GeneralSecurityException.class,()->AppUpdates.verified(payload,signature));
        assertThrows(java.security.GeneralSecurityException.class,()->AppUpdates.verified(new byte[32769],signature));
        assertThrows(java.security.GeneralSecurityException.class,()->AppUpdates.verified(new byte[1],new byte[73]));
    }
    @Test public void bridgeCannotChooseInstallerOrReleaseAddress(){
        for(String op:new String[]{"app_update_check","app_update_state","app_update_download","app_update_install","dashboard_discover"}){
            assertEquals(op,StrictRequest.validated("{\"id\":1,\"op\":\""+op+"\"}").get("op"));
            assertThrows(IllegalArgumentException.class,()->StrictRequest.validated("{\"id\":1,\"op\":\""+op+"\",\"url\":\"https://example.invalid\"}"));
        }
    }
}
