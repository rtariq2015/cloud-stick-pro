/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.security.*;
import java.security.interfaces.ECPublicKey;
import java.security.spec.ECGenParameterSpec;

/** Only public pairing bindings are stored in preferences; signing keys stay in AndroidKeyStore. */
final class WakeKeys {
    private final SharedPreferences bindings;
    WakeKeys(Context context){bindings=context.getSharedPreferences("wake-pairings",Context.MODE_PRIVATE);}
    boolean hasPairings(){return !bindings.getAll().isEmpty();}
    private String alias(String device){
        if(device==null||!device.matches("(?:[0-9A-F]{2}:){5}[0-9A-F]{2}"))throw new IllegalArgumentException();
        return "cloud-stick-wake-"+device.replace(":","");
    }
    private KeyStore store()throws GeneralSecurityException{
        try{KeyStore store=KeyStore.getInstance("AndroidKeyStore");store.load(null);return store;}catch(java.io.IOException e){throw new GeneralSecurityException();}
    }
    synchronized byte[] publicKey(String device)throws GeneralSecurityException{
        String name=alias(device);KeyStore store=store();
        if(!store.containsAlias(name)){
            KeyPairGenerator generator=KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC,"AndroidKeyStore");
            generator.initialize(new KeyGenParameterSpec.Builder(name,KeyProperties.PURPOSE_SIGN).setAlgorithmParameterSpec(new ECGenParameterSpec("secp256r1")).setDigests(KeyProperties.DIGEST_SHA256).build());
            generator.generateKeyPair();
        }
        return WakeProtocol.publicKey((ECPublicKey)store().getCertificate(name).getPublicKey());
    }
    synchronized void remember(String device,String id)throws GeneralSecurityException{
        WakeProtocol.hex(id,16,16);if(!store().containsAlias(alias(device)))throw new GeneralSecurityException();
        // A new binding for this board replaces its old owner-revision binding.
        SharedPreferences.Editor edit=bindings.edit();for(var entry:bindings.getAll().entrySet())if(device.equals(entry.getValue()))edit.remove(entry.getKey());
        if(!edit.putString(id,device).commit())throw new GeneralSecurityException();
    }
    synchronized String device(String id){WakeProtocol.hex(id,16,16);String device=bindings.getString(id,null);if(device!=null)alias(device);return device;}
    synchronized byte[] sign(String id,String nonce)throws GeneralSecurityException{
        String device=device(id);if(device==null)throw new GeneralSecurityException();
        PrivateKey key=(PrivateKey)store().getKey(alias(device),null);if(key==null)throw new GeneralSecurityException();return WakeProtocol.sign(key,id,nonce);
    }
}
