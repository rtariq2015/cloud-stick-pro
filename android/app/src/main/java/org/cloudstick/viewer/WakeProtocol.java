/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.security.interfaces.ECPublicKey;
import java.math.BigInteger;
import java.util.*;

/** Domain-separated proof opens a finite setup window; it never carries a password. */
final class WakeProtocol {
    private static final byte[] DOMAIN="CloudStickWake/v1\0".getBytes(StandardCharsets.US_ASCII);
    static byte[] hex(String text,int min,int max){
        if(text==null||text.length()%2!=0||text.length()<min*2||text.length()>max*2||!text.matches("[0-9a-f]+"))throw new IllegalArgumentException();
        byte[] out=new byte[text.length()/2];for(int i=0;i<out.length;i++)out[i]=(byte)Integer.parseInt(text.substring(i*2,i*2+2),16);return out;
    }
    static String hex(byte[] bytes){StringBuilder out=new StringBuilder(bytes.length*2);for(byte b:bytes)out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();}
    static byte[] message(String id,String nonce){
        byte[] identity=hex(id,16,16),random=hex(nonce,32,32),out=new byte[66];
        System.arraycopy(DOMAIN,0,out,0,18);System.arraycopy(identity,0,out,18,16);System.arraycopy(random,0,out,34,32);return out;
    }
    private static byte[] coordinate(BigInteger value){
        byte[] raw=value.toByteArray(),out=new byte[32];if(value.signum()<0||raw.length>33||raw.length==33&&raw[0]!=0)throw new IllegalArgumentException();
        int count=Math.min(32,raw.length);System.arraycopy(raw,raw.length-count,out,32-count,count);return out;
    }
    static byte[] publicKey(ECPublicKey key){
        if(key.getParams().getCurve().getField().getFieldSize()!=256)throw new IllegalArgumentException();
        byte[] out=new byte[65];out[0]=4;System.arraycopy(coordinate(key.getW().getAffineX()),0,out,1,32);System.arraycopy(coordinate(key.getW().getAffineY()),0,out,33,32);return out;
    }
    static byte[] sign(PrivateKey key,String id,String nonce)throws GeneralSecurityException{
        Signature signature=Signature.getInstance("SHA256withECDSA");signature.initSign(key);signature.update(message(id,nonce));return signature.sign();
    }
    static String proof(String id,String nonce,byte[] signature){
        message(id,nonce);if(signature.length<8||signature.length>72)throw new IllegalArgumentException();
        return "{\"v\":1,\"id\":\""+id+"\",\"nonce\":\""+nonce+"\",\"signature\":\""+hex(signature)+"\"}";
    }
}
