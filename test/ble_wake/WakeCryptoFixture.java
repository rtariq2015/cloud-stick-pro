/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import java.nio.ByteBuffer;
import java.security.*;
import java.security.interfaces.ECPublicKey;
import java.security.spec.ECGenParameterSpec;
import java.util.*;
/** Runtime-generated synthetic key; only its public key and proof leave this process. */
public final class WakeCryptoFixture {
    public static void main(String[] args)throws Exception{
        KeyPairGenerator generator=KeyPairGenerator.getInstance("EC");generator.initialize(new ECGenParameterSpec("secp256r1"));KeyPair key=generator.generateKeyPair();
        byte[] publicKey=WakeProtocol.publicKey((ECPublicKey)key.getPublic());
        ByteBuffer record=ByteBuffer.allocate(83);record.put(new byte[]{'C','W','K','1',1,2,3,4,5,6});record.putLong(123);record.put(publicKey);
        String id=WakeProtocol.hex(Arrays.copyOf(MessageDigest.getInstance("SHA-256").digest(record.array()),16)),nonce="42".repeat(32);
        System.out.println(WakeProtocol.hex(publicKey));System.out.println(id);System.out.println(nonce);System.out.println(WakeProtocol.hex(WakeProtocol.sign(key.getPrivate(),id,nonce)));
        System.out.println(WakeProtocol.hex(WakeProtocol.publicKey((ECPublicKey)generator.generateKeyPair().getPublic())));
    }
}
