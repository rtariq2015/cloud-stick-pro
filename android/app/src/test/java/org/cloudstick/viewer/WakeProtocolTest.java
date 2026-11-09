/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import org.junit.Test;
import static org.junit.Assert.*;
import java.security.*;
import java.security.interfaces.ECPublicKey;
import java.security.spec.ECGenParameterSpec;
import java.util.*;

public class WakeProtocolTest {
    static final String ID="0123456789abcdef0123456789abcdef",NONCE="01".repeat(32);
    static KeyPair keys()throws Exception{KeyPairGenerator g=KeyPairGenerator.getInstance("EC");g.initialize(new ECGenParameterSpec("secp256r1"));return g.generateKeyPair();}
    @Test public void exactSharedTranscriptHasDomainIdentityAndNonce(){
        String expected="436c6f7564537469636b57616b652f763100"+ID+NONCE;
        assertEquals(expected,WakeProtocol.hex(WakeProtocol.message(ID,NONCE)));assertEquals(66,WakeProtocol.message(ID,NONCE).length);
    }
    @Test public void realSignatureCannotBeMovedToAnotherDeviceNonceOrKey()throws Exception{
        KeyPair pair=keys();byte[] signature=WakeProtocol.sign(pair.getPrivate(),ID,NONCE);
        Signature verifier=Signature.getInstance("SHA256withECDSA");verifier.initVerify(pair.getPublic());verifier.update(WakeProtocol.message(ID,NONCE));assertTrue(verifier.verify(signature));
        for(String[] changed:new String[][]{{"ff".repeat(16),NONCE},{ID,"02".repeat(32)}}){verifier.initVerify(pair.getPublic());verifier.update(WakeProtocol.message(changed[0],changed[1]));assertFalse(verifier.verify(signature));}
        verifier.initVerify(keys().getPublic());verifier.update(WakeProtocol.message(ID,NONCE));assertFalse(verifier.verify(signature));
        byte[] publicKey=WakeProtocol.publicKey((ECPublicKey)pair.getPublic());assertEquals(65,publicKey.length);assertEquals(4,publicKey[0]);
    }
    @Test public void malformedTranscriptsNeverReachSigning(){
        for(String id:new String[]{"",ID.substring(1),ID.toUpperCase(Locale.ROOT),"zz".repeat(16),ID+"00"})assertThrows(IllegalArgumentException.class,()->WakeProtocol.message(id,NONCE));
        for(String nonce:new String[]{"",NONCE.substring(2),NONCE+"00","g0".repeat(32)})assertThrows(IllegalArgumentException.class,()->WakeProtocol.message(ID,nonce));
    }
    @Test public void canonicalProofCarriesOnlySignatureAndPublicChallenge(){
        byte[] signature=new byte[72];String proof=WakeProtocol.proof(ID,NONCE,signature);
        assertEquals(Set.of("v","id","nonce","signature"),StrictRequest.parse(proof).keySet());assertFalse(proof.contains("password"));
        assertThrows(IllegalArgumentException.class,()->WakeProtocol.proof(ID,NONCE,new byte[73]));
    }
}
