/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Cloud Stick Pro contributors
 */
package org.cloudstick.viewer;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.math.BigInteger;
import java.util.*;
import java.util.regex.*;
import java.util.function.LongSupplier;

/** Fixed, credential-free GET to a user-selected private IPv4 address. Never executes remote content. */
final class DashboardClient implements AutoCloseable {
    interface Connector { HttpURLConnection open(URL url) throws IOException; }
    private final Connector connector;
    private final LongSupplier clock;
    private volatile HttpURLConnection active;
    private volatile boolean closed;
    DashboardClient(){this(url->(HttpURLConnection)url.openConnection(Proxy.NO_PROXY));}
    DashboardClient(Connector connector){this(connector,System::nanoTime);}
    DashboardClient(Connector connector,LongSupplier clock){this.connector=connector;this.clock=clock;}
    static String address(String input) {
        if(input==null || input.length()>64)throw new IllegalArgumentException();
        String value=input.trim();
        if(value.startsWith("http://"))value=value.substring(7);
        if(value.endsWith("/"))value=value.substring(0,value.length()-1);
        if(!value.matches("(?:0|[1-9][0-9]{0,2})(?:\\.(?:0|[1-9][0-9]{0,2})){3}"))throw new IllegalArgumentException();
        String[] parts=value.split("\\.");int[] bytes=new int[4];
        for(int i=0;i<4;i++){bytes[i]=Integer.parseInt(parts[i]);if(bytes[i]>255)throw new IllegalArgumentException();}
        if(!(bytes[0]==10 || bytes[0]==192&&bytes[1]==168 || bytes[0]==172&&bytes[1]>=16&&bytes[1]<=31))throw new IllegalArgumentException();
        return value;
    }
    Map<String,Object> read(String savedAddress) throws IOException {
        String host=address(savedAddress);
        if(closed)throw new IOException();
        HttpURLConnection connection=connector.open(new URL("http://"+host+"/status"));active=connection;
        try {
            if(closed)throw new IOException();
            long deadline=clock.getAsLong()+5_000_000_000L;
            connection.setConnectTimeout(1500);connection.setReadTimeout(1500);
            connection.setInstanceFollowRedirects(false);connection.setUseCaches(false);
            connection.setRequestMethod("GET");connection.setRequestProperty("Accept","application/json");
            connection.setRequestProperty("Accept-Encoding","identity");connection.setRequestProperty("Connection","close");
            if(connection.getResponseCode()!=200)throw new IOException();
            String mime=connection.getContentType(),encoding=connection.getContentEncoding();
            if(mime==null || !mime.split(";",2)[0].trim().equalsIgnoreCase("application/json") || encoding!=null&&!encoding.equalsIgnoreCase("identity"))throw new IOException();
            long length=connection.getContentLengthLong();if(length>4096)throw new IOException();
            ByteArrayOutputStream body=new ByteArrayOutputStream();
            try(InputStream in=connection.getInputStream()) {
                byte[] buffer=new byte[512];int n;
                while((n=in.read(buffer))!=-1){
                    if(closed || Thread.currentThread().isInterrupted() || clock.getAsLong()>deadline || body.size()+n>4096)throw new IOException();
                    body.write(buffer,0,n);
                }
            }
            byte[] bytes=body.toByteArray();
            if(closed || clock.getAsLong()>deadline || length>=0&&length!=bytes.length)throw new IOException();
            for(byte b:bytes)if(b<32&&b!=9&&b!=10&&b!=13 || b>126 || b<0)throw new IOException();
            try{return status(new String(bytes,StandardCharsets.US_ASCII));}catch(IllegalArgumentException e){throw new IOException();}
        }finally{connection.disconnect();active=null;}
    }
    @Override public void close(){closed=true;HttpURLConnection connection=active;if(connection!=null)connection.disconnect();}
    private static final Set<String> FLAGS=Set.of("wifi","media","active","scanning","has_scan","scan_complete","window_open","paused","cloud","configured");
    private static final Set<String> NUMBERS=Set.of("uptime_ms","verified","pending","failed","done","total","bytes_per_second","quiet_seconds","last_backup_unix","error","percent","phase");
    private static final Pattern FIELD=Pattern.compile("\\s*\"([a-z_]+)\"\\s*:\\s*(\"[A-Za-z0-9.-]{1,48}\"|true|false|-?(?:0|[1-9][0-9]{0,19}))\\s*");
    static Map<String,Object> status(String raw) {
        if(raw==null || raw.length()>4096)throw new IllegalArgumentException();
        String text=raw.trim();if(!text.startsWith("{")||!text.endsWith("}"))throw new IllegalArgumentException();
        Map<String,Object> result=new HashMap<>();int at=1,end=text.length()-1;
        while(at<end){
            Matcher match=FIELD.matcher(text);match.region(at,end);if(!match.lookingAt())throw new IllegalArgumentException();
            String name=match.group(1),value=match.group(2);Object normalized;
            if(name.equals("firmware")){if(!value.startsWith("\""))throw new IllegalArgumentException();normalized=value.substring(1,value.length()-1);}
            else if(FLAGS.contains(name)){if(!value.equals("true")&&!value.equals("false"))throw new IllegalArgumentException();normalized=value.equals("true");}
            else if(NUMBERS.contains(name)){
                BigInteger number;try{number=new BigInteger(value);}catch(NumberFormatException e){throw new IllegalArgumentException();}
                BigInteger min=BigInteger.ZERO,max=new BigInteger("4294967295");
                if(name.equals("error")){min=BigInteger.valueOf(Integer.MIN_VALUE);max=BigInteger.valueOf(Integer.MAX_VALUE);}
                if(name.equals("percent")){min=BigInteger.valueOf(-1);max=BigInteger.valueOf(100);}
                if(name.equals("phase"))max=BigInteger.valueOf(4);
                if(name.equals("last_backup_unix"))max=BigInteger.valueOf(253402300799L);
                if(name.equals("done")||name.equals("total"))max=new BigInteger("18446744073709551615");
                if(number.compareTo(min)<0||number.compareTo(max)>0)throw new IllegalArgumentException();
                normalized=name.equals("done")||name.equals("total")?number.toString():number.longValueExact();
            }else throw new IllegalArgumentException();
            if(result.putIfAbsent(name,normalized)!=null)throw new IllegalArgumentException();
            at=match.end();if(at==end)break;if(text.charAt(at++)!=','||at==end)throw new IllegalArgumentException();
        }
        Set<String> keys=new HashSet<>(FLAGS);keys.addAll(NUMBERS);keys.add("firmware");
        if(!result.keySet().equals(keys))throw new IllegalArgumentException();return result;
    }
}
