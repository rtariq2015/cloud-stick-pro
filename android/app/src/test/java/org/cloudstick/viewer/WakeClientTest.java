/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import org.junit.Test;
import static org.junit.Assert.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.util.*;

public class WakeClientTest {
    static final String ID=WakeProtocolTest.ID,NONCE=WakeProtocolTest.NONCE,DEVICE="AA:BB:CC:DD:EE:FF";
    static final String CHALLENGE="{\"v\":1,\"id\":\""+ID+"\",\"nonce\":\""+NONCE+"\"}";
    static final String OPENING="{\"v\":1,\"state\":\"opening\"}";
    static class Response extends HttpURLConnection {
        int status=200;String type="application/json",encoding;byte[] body;long declared;boolean disconnected;ByteArrayOutputStream sent=new ByteArrayOutputStream();
        Response(String body)throws Exception{super(new URL("http://192.168.1.42:81/challenge"));this.body=body.getBytes(StandardCharsets.US_ASCII);declared=this.body.length;}
        public void connect(){}public boolean usingProxy(){return false;}public void disconnect(){disconnected=true;}
        public int getResponseCode(){return status;}public String getContentType(){return type;}public String getContentEncoding(){return encoding;}
        public long getContentLengthLong(){return declared;}public InputStream getInputStream(){return new ByteArrayInputStream(body);}public OutputStream getOutputStream(){return sent;}
    }
    static class Keys implements WakeClient.Keys {
        final KeyPair key;boolean known=true;int signatures;
        Keys()throws Exception{key=WakeProtocolTest.keys();}
        public String device(String id){assertEquals(ID,id);return known?DEVICE:null;}
        public byte[] sign(String id,String nonce)throws GeneralSecurityException{++signatures;return WakeProtocol.sign(key.getPrivate(),id,nonce);}
    }
    @Test public void onlyTrustedKeySignsAndOnlyFixedPortPathsAreUsed()throws Exception{
        Response challenge=new Response(CHALLENGE),opening=new Response(OPENING);Keys keys=new Keys();List<String> urls=new ArrayList<>();
        try(WakeClient client=new WakeClient(url->{urls.add(url.toString());return urls.size()==1?challenge:opening;})){
            assertEquals(DEVICE,client.wake("192.168.1.42",keys));assertEquals(List.of("http://192.168.1.42:81/challenge","http://192.168.1.42:81/open"),urls);
            assertEquals("POST",opening.getRequestMethod());assertEquals("application/json",opening.getRequestProperty("Content-Type"));
            Map<String,Object> proof=StrictRequest.parse(opening.sent.toString(StandardCharsets.US_ASCII));Signature verifier=Signature.getInstance("SHA256withECDSA");verifier.initVerify(keys.key.getPublic());verifier.update(WakeProtocol.message(ID,NONCE));assertTrue(verifier.verify(WakeProtocol.hex((String)proof.get("signature"),8,72)));
            for(Response response:List.of(challenge,opening)){assertFalse(response.getInstanceFollowRedirects());assertEquals(1500,response.getReadTimeout());assertEquals(1500,response.getConnectTimeout());assertNull(response.getRequestProperty("Authorization"));assertNull(response.getRequestProperty("Cookie"));assertTrue(response.disconnected);}
        }
    }
    @Test public void unknownPairingNeverSignsOrPosts()throws Exception{
        Response challenge=new Response(CHALLENGE);Keys keys=new Keys();keys.known=false;int[] calls={0};
        try(WakeClient client=new WakeClient(url->{calls[0]++;return challenge;})){assertEquals("wake_not_paired",assertThrows(WakeClient.Failure.class,()->client.wake("10.0.0.1",keys)).code);assertEquals(1,calls[0]);assertEquals(0,keys.signatures);}
    }
    @Test public void responseAmbiguityCannotTriggerSigning()throws Exception{
        for(String body:List.of(CHALLENGE.replace("\"v\":1","\"v\":2"),CHALLENGE.replace("\"v\":1","\"v\":1,\"v\":1"),CHALLENGE+"{}",CHALLENGE.replace(ID,ID+"00"),CHALLENGE.replace(NONCE,NONCE.substring(2)),CHALLENGE.replace("}",",\"extra\":1}"))){
            Response response=new Response(body);Keys keys=new Keys();try(WakeClient client=new WakeClient(url->response)){assertThrows(IOException.class,()->client.wake("10.0.0.1",keys));assertEquals(0,keys.signatures);}
        }
    }
    @Test public void redirectsFramingTypesAndCompressionRejected()throws Exception{
        for(int code:new int[]{301,302,307,308,401,403,404,409,500,503}){Response response=new Response(CHALLENGE);response.status=code;Keys keys=new Keys();try(WakeClient client=new WakeClient(url->response)){assertThrows(IOException.class,()->client.wake("10.0.0.1",keys));assertEquals(0,keys.signatures);}}
        for(int mode=0;mode<5;mode++){Response response=new Response(CHALLENGE);if(mode==0)response.type="text/html";if(mode==1)response.encoding="gzip";if(mode==2)response.declared=-1;if(mode==3)response.declared=513;if(mode==4)response.declared=response.body.length+1;Keys keys=new Keys();try(WakeClient client=new WakeClient(url->response)){assertThrows(IOException.class,()->client.wake("10.0.0.1",keys));assertEquals(0,keys.signatures);}}
    }
    @Test public void invalidAddressOrCancelledClientNeverOpensSocket()throws Exception{
        Keys keys=new Keys();try(WakeClient client=new WakeClient(url->{throw new AssertionError();})){assertThrows(IllegalArgumentException.class,()->client.wake("8.8.8.8",keys));client.close();assertThrows(IOException.class,()->client.wake("10.0.0.1",keys));}
    }
}
