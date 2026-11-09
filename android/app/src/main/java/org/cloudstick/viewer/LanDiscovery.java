/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.*;
/** Three-second broadcast search. Replies are hints, never authenticated identity. */
final class LanDiscovery implements AutoCloseable {
    private volatile DatagramSocket socket;
    private volatile boolean closed;
    List<String> find()throws Exception {
        byte[] random=new byte[16];new SecureRandom().nextBytes(random);
        String nonce=WakeProtocol.hex(random);
        byte[] query=("CSP-DISCOVER/1 "+nonce).getBytes(StandardCharsets.US_ASCII);
        byte[] expected=("CSP-FOUND/1 "+nonce).getBytes(StandardCharsets.US_ASCII);
        Set<String> destinations=new LinkedHashSet<>();destinations.add("255.255.255.255");
        var interfaces=NetworkInterface.getNetworkInterfaces();
        while(interfaces!=null&&interfaces.hasMoreElements()&&destinations.size()<8){
            var adapter=interfaces.nextElement();if(!adapter.isUp()||adapter.isLoopback())continue;
            for(var address:adapter.getInterfaceAddresses())if(address.getBroadcast() instanceof Inet4Address&&destinations.size()<8){
                try{DashboardClient.address(address.getAddress().getHostAddress());destinations.add(address.getBroadcast().getHostAddress());}catch(IllegalArgumentException ignored){}
            }
        }
        TreeSet<String> found=new TreeSet<>();
        try(DatagramSocket channel=new DatagramSocket()){
            socket=channel;channel.setBroadcast(true);channel.setSoTimeout(200);
            long deadline=System.nanoTime()+3_000_000_000L,next=0;
            while(!closed&&System.nanoTime()<deadline&&found.size()<16){
                if(System.nanoTime()>=next){for(String host:destinations)try{channel.send(new DatagramPacket(query,query.length,InetAddress.getByName(host),37219));}catch(java.io.IOException ignored){}next=System.nanoTime()+1_000_000_000L;}
                byte[] b=new byte[128];DatagramPacket reply=new DatagramPacket(b,b.length);
                try{channel.receive(reply);if(reply.getPort()==37219&&reply.getLength()==expected.length&&Arrays.equals(Arrays.copyOf(b,reply.getLength()),expected))try{found.add(DashboardClient.address(reply.getAddress().getHostAddress()));}catch(IllegalArgumentException ignored){}}
                catch(SocketTimeoutException ignored){}
            }
        }finally{socket=null;}
        if(closed)throw new java.io.IOException();return new ArrayList<>(found);
    }
    @Override public void close(){closed=true;DatagramSocket channel=socket;if(channel!=null)channel.close();}
}
