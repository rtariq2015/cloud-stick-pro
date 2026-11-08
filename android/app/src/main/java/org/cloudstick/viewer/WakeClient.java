/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.util.*;

/** Fixed authenticated wake protocol on port 81. Public status stays on port 80. */
final class WakeClient implements AutoCloseable {
    static final class Failure extends IOException {final String code;Failure(String code){super(code);this.code=code;}}
    interface Keys {String device(String id);byte[] sign(String id,String nonce)throws GeneralSecurityException;}
    private volatile boolean closed;
    private volatile HttpURLConnection active;
    private final DashboardClient.Connector connector;
    WakeClient(){this(url->(HttpURLConnection)url.openConnection(Proxy.NO_PROXY));}
    WakeClient(DashboardClient.Connector connector){this.connector=connector;}
    private Map<String,Object> exchange(String host,String path,String body)throws IOException{
        if(closed||Thread.currentThread().isInterrupted())throw new Failure("cancelled");
        HttpURLConnection connection=connector.open(new URL("http://"+host+":81"+path));active=connection;
        try{
            long deadline=System.nanoTime()+5_000_000_000L;
            connection.setConnectTimeout(1500);connection.setReadTimeout(1500);connection.setInstanceFollowRedirects(false);connection.setUseCaches(false);
            connection.setRequestProperty("Accept","application/json");connection.setRequestProperty("Accept-Encoding","identity");connection.setRequestProperty("Connection","close");
            if(body!=null){
                byte[] bytes=body.getBytes(StandardCharsets.US_ASCII);connection.setRequestMethod("POST");connection.setDoOutput(true);connection.setFixedLengthStreamingMode(bytes.length);connection.setRequestProperty("Content-Type","application/json");
                if(closed)throw new Failure("cancelled");try(OutputStream output=connection.getOutputStream()){output.write(bytes);}
            }else connection.setRequestMethod("GET");
            int status=connection.getResponseCode();if(status==404||status==403)throw new Failure("wake_not_paired");if(status==409||status==503)throw new Failure("wake_busy");if(status!=200)throw new Failure("wake_rejected");
            String type=connection.getContentType(),encoding=connection.getContentEncoding();long length=connection.getContentLengthLong();
            if(type==null||!type.split(";",2)[0].trim().equalsIgnoreCase("application/json")||encoding!=null&&!encoding.equalsIgnoreCase("identity")||length<1||length>512)throw new Failure("wake_invalid_response");
            byte[] bytes=new byte[(int)length];try(InputStream input=connection.getInputStream()){
                int at=0;while(at<bytes.length){if(closed||Thread.currentThread().isInterrupted()||System.nanoTime()>deadline)throw new Failure("cancelled");int n=input.read(bytes,at,bytes.length-at);if(n<0)throw new Failure("wake_invalid_response");at+=n;}
                // Content-Length is authoritative; closing now lets the embedded
                // server take the passive TCP-close path without waiting for EOF.
            }
            if(closed||System.nanoTime()>deadline)throw new Failure("cancelled");
            for(byte b:bytes)if(b<32||b>126)throw new Failure("wake_invalid_response");
            try{return StrictRequest.parse(new String(bytes,StandardCharsets.US_ASCII));}catch(RuntimeException e){throw new Failure("wake_invalid_response");}
        }finally{connection.disconnect();active=null;}
    }
    String wake(String address,Keys keys)throws IOException{
        String host=DashboardClient.address(address);
        Map<String,Object> challenge=exchange(host,"/challenge",null);
        if(!challenge.keySet().equals(Set.of("v","id","nonce"))||!Integer.valueOf(1).equals(challenge.get("v"))||!(challenge.get("id") instanceof String)||!(challenge.get("nonce") instanceof String))throw new Failure("wake_invalid_response");
        String id=(String)challenge.get("id"),nonce=(String)challenge.get("nonce");
        try{
            WakeProtocol.message(id,nonce);String device=keys.device(id);if(device==null)throw new Failure("wake_not_paired");
            byte[] signature=keys.sign(id,nonce);Map<String,Object> reply=exchange(host,"/open",WakeProtocol.proof(id,nonce,signature));
            if(!reply.keySet().equals(Set.of("v","state"))||!Integer.valueOf(1).equals(reply.get("v"))||!"opening".equals(reply.get("state")))throw new Failure("wake_invalid_response");return device;
        }catch(GeneralSecurityException|IllegalArgumentException e){throw new Failure("wake_key_unavailable");}
    }
    @Override public void close(){closed=true;HttpURLConnection connection=active;if(connection!=null)connection.disconnect();}
}
