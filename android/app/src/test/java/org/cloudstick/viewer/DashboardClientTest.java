/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Cloud Stick Pro contributors
 */
package org.cloudstick.viewer;
import org.junit.Test;
import static org.junit.Assert.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class DashboardClientTest {
    static final String STATUS="{\"firmware\":\"0.4.6-cache-dev\",\"uptime_ms\":123,\"wifi\":true,\"media\":true,\"active\":false,\"scanning\":false,\"has_scan\":true,\"scan_complete\":true,\"window_open\":true,\"paused\":false,\"cloud\":true,\"configured\":true,\"verified\":12,\"pending\":0,\"failed\":0,\"done\":2097152,\"total\":2097152,\"bytes_per_second\":0,\"quiet_seconds\":0,\"last_backup_unix\":1794000000,\"error\":0,\"percent\":100,\"phase\":0}";
    static class Response extends HttpURLConnection {
        int code=200;String mime="application/json; charset=utf-8",encoding;byte[] body=STATUS.getBytes(StandardCharsets.US_ASCII);long declared=-1;boolean disconnected;
        Response()throws Exception{super(new URL("http://192.168.1.42/status"));}
        @Override public void connect(){}
        @Override public void disconnect(){disconnected=true;}
        @Override public boolean usingProxy(){return false;}
        @Override public int getResponseCode(){return code;}
        @Override public String getContentType(){return mime;}
        @Override public String getContentEncoding(){return encoding;}
        @Override public long getContentLengthLong(){return declared;}
        @Override public InputStream getInputStream()throws IOException{return new ByteArrayInputStream(body);}
    }
    @Test public void canonicalPrivateAddressesAndBrowserPasteAccepted(){for(String input:new String[]{"10.0.0.1","172.16.0.2","172.31.255.254","192.168.1.42"})assertEquals(input,DashboardClient.address(input));assertEquals("192.168.1.42",DashboardClient.address(" http://192.168.1.42/ "));}
    @Test public void publicLoopbackAmbiguousAndCredentialAddressesRejected(){for(String input:new String[]{"","8.8.8.8","127.0.0.1","169.254.1.1","172.15.0.1","172.32.0.1","192.169.1.1","192.168.001.1","192.168.1.256","0xC0A80101","3232235777","cloud-stick.local","https://192.168.1.1","http://owner:pass@192.168.1.1","http://192.168.1.1:80","192.168.1.1/status","192.168.1.1?x","192.168.1.1#x","[::1]","192.168.1.1\n/status"})assertThrows(input,IllegalArgumentException.class,()->DashboardClient.address(input));}
    @Test public void statusExactFieldsTypesAndUnsignedCounts(){Map<String,Object> state=DashboardClient.status(STATUS);assertEquals(12L,state.get("verified"));assertEquals("2097152",state.get("done"));assertEquals(true,state.get("wifi"));assertEquals("18446744073709551615",DashboardClient.status(STATUS.replace("\"done\":2097152","\"done\":18446744073709551615")).get("done"));}
    @Test public void malformedOrPrivateStatusNeverCrossesBridge(){for(String raw:new String[]{"{}",STATUS+"null",STATUS.replace("\"wifi\":true","\"wifi\":1"),STATUS.replace("\"verified\":12","\"verified\":12.0"),STATUS.replace("\"verified\":12","\"verified\":-1"),STATUS.replace("\"percent\":100","\"percent\":101"),STATUS.replace("\"phase\":0","\"phase\":5"),STATUS.replace("\"done\":2097152","\"done\":18446744073709551616"),STATUS.replace("\"done\":2097152","\"done\":\"2097152\""),STATUS.replace("\"error\":0","\"error\":2147483648"),STATUS.replace("\"phase\":0}","\"phase\":0,}"),STATUS.replace("\"wifi\":true","\"wifi\":true,\"wifi\":false"),STATUS.replace("\"phase\":0}","\"phase\":0,\"password\":\"secret\"}"),STATUS.replace("\"phase\":0}","\"phase\":0,\"files\":[]}")})assertThrows(raw,IllegalArgumentException.class,()->DashboardClient.status(raw));}
    @Test public void onlyFixedGetNoRedirectsFiniteTimeoutAndNoCredentials()throws Exception{
        Response response=new Response();try(DashboardClient client=new DashboardClient(url->{assertEquals("http://192.168.1.42/status",url.toString());return response;})){
            assertEquals(12L,client.read("192.168.1.42").get("verified"));
            assertEquals("GET",response.getRequestMethod());assertFalse(response.getInstanceFollowRedirects());assertFalse(response.getUseCaches());assertEquals(1500,response.getConnectTimeout());assertEquals(1500,response.getReadTimeout());assertEquals("identity",response.getRequestProperty("Accept-Encoding"));assertNull(response.getRequestProperty("Authorization"));assertNull(response.getRequestProperty("Cookie"));assertTrue(response.disconnected);
        }
    }
    @Test public void redirectsAndNonJsonAndCompressedResponsesRejected()throws Exception{
        for(int code:new int[]{301,302,307,308,401,404,500}){Response response=new Response();response.code=code;try(DashboardClient client=new DashboardClient(url->response)){assertThrows(IOException.class,()->client.read("10.0.0.1"));assertTrue(response.disconnected);}}
        for(String mime:new String[]{"text/html","text/javascript",null}){Response response=new Response();response.mime=mime;try(DashboardClient client=new DashboardClient(url->response)){assertThrows(IOException.class,()->client.read("10.0.0.1"));}}
        Response response=new Response();response.encoding="gzip";try(DashboardClient client=new DashboardClient(url->response)){assertThrows(IOException.class,()->client.read("10.0.0.1"));}
    }
    @Test public void oversizedTruncatedNonAsciiAndMalformedBodiesRejected()throws Exception{
        for(byte[] body:new byte[][]{new byte[4097],"<script>bad</script>".getBytes(StandardCharsets.US_ASCII),STATUS.replace("cache","caché").getBytes(StandardCharsets.UTF_8)}){Response response=new Response();response.body=body;try(DashboardClient client=new DashboardClient(url->response)){assertThrows(IOException.class,()->client.read("10.0.0.1"));}}
        for(long length:new long[]{4097,1}){Response response=new Response();response.declared=length;try(DashboardClient client=new DashboardClient(url->response)){assertThrows(IOException.class,()->client.read("10.0.0.1"));}}
    }
    @Test public void slowDripCannotRenewTotalDeadline()throws Exception{
        Response response=new Response(){@Override public InputStream getInputStream(){return new ByteArrayInputStream(body){@Override public synchronized int read(byte[] data,int off,int len){return super.read(data,off,Math.min(1,len));}};}};
        java.util.concurrent.atomic.AtomicLong time=new java.util.concurrent.atomic.AtomicLong();
        try(DashboardClient client=new DashboardClient(url->response,()->time.getAndAdd(2_000_000_000L))){assertThrows(IOException.class,()->client.read("10.0.0.1"));assertTrue(response.disconnected);}
    }
    @Test public void cancellationBeforeConnectionOpensNothing()throws Exception{try(DashboardClient client=new DashboardClient(url->{throw new AssertionError("must not connect");})){client.close();assertThrows(IOException.class,()->client.read("192.168.1.1"));}}
    @Test public void invalidAddressRejectedBeforeTransport()throws Exception{try(DashboardClient client=new DashboardClient(url->{throw new AssertionError("must not connect");})){assertThrows(IllegalArgumentException.class,()->client.read("example.com"));}}
    @Test public void interruptedReadAndTransportFailureCloseConnection()throws Exception{
        Response response=new Response();try(DashboardClient client=new DashboardClient(url->response)){Thread.currentThread().interrupt();try{assertThrows(IOException.class,()->client.read("10.0.0.1"));assertTrue(response.disconnected);}finally{Thread.interrupted();}}
        Response broken=new Response(){@Override public InputStream getInputStream()throws IOException{throw new IOException("synthetic failure");}};try(DashboardClient client=new DashboardClient(url->broken)){assertThrows(IOException.class,()->client.read("10.0.0.1"));assertTrue(broken.disconnected);}
    }
}
