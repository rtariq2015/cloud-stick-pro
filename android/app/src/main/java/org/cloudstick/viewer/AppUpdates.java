/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;

import android.content.Context;
import android.content.pm.PackageManager;
import org.json.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.security.spec.X509EncodedKeySpec;
import java.util.*;
import java.util.concurrent.*;
import javax.net.ssl.HttpsURLConnection;

/** Fixed signed release feed. Downloads cannot choose a package, URL or signer. */
final class AppUpdates implements AutoCloseable {
    static final String FEED="https://raw.githubusercontent.com/rtariq2015/cloud-stick-pro/main/desktop/updates/catalog.json";
    static final String KEY="3059301306072a8648ce3d020106082a8648ce3d030107034200044711d64b9f6da526b474fa3dbd0f9a5dd1eb1cd0380760daf174dbe03d5478726ec16e79ab4bb31947584f7b4f4f73998306e126c36791030bb1b6fc378c28a5";
    static final String SIGNER="329de97b012492de6765270a573f34535130f103a24cf0dd2180abaee2ffaad8";
    private final Context context;
    private final ExecutorService executor=Executors.newSingleThreadExecutor();
    private volatile HttpsURLConnection connection;
    private volatile boolean closed;
    private String phase="idle",offered="";
    private long done,total=1;
    private JSONObject release;
    private final File apk;
    AppUpdates(Context context){this.context=context.getApplicationContext();apk=new File(context.getFilesDir(),"update.apk");}
    synchronized Map<String,Object> state(){return Map.of("phase",phase,"current",BuildConfig.VERSION_NAME,"version",offered,"done",done,"total",total);}
    private static byte[] decodeKey(){byte[] b=new byte[KEY.length()/2];for(int i=0;i<b.length;i++)b[i]=(byte)Integer.parseInt(KEY.substring(i*2,i*2+2),16);return b;}
    static byte[] verified(byte[] payload,byte[] signature)throws GeneralSecurityException {
        if(payload.length>32768||signature.length<64||signature.length>72)throw new SignatureException();
        Signature verifier=Signature.getInstance("SHA256withECDSA");
        verifier.initVerify(KeyFactory.getInstance("EC").generatePublic(new X509EncodedKeySpec(decodeKey())));
        verifier.update("CloudStickAppCatalog/v1\0".getBytes(StandardCharsets.US_ASCII));verifier.update(payload);
        if(!verifier.verify(signature))throw new SignatureException();return payload;
    }
    synchronized Map<String,Object> start(boolean download){
        if(closed||phase.equals("checking")||phase.equals("downloading"))return state();
        if(download&&(!phase.equals("available")||release==null))return state();
        phase=download?"downloading":"checking";done=0;total=1;
        executor.execute(()->{try{if(download)download();else check();}catch(Exception ignored){synchronized(this){phase="failed";}}});return state();
    }
    private InputStream open(String url)throws IOException {
        URL target=new URL(url);
        for(int redirects=0;redirects<5;redirects++){
            if(closed||!target.getProtocol().equals("https")||target.getUserInfo()!=null||target.getRef()!=null||target.getPort()!=-1&&target.getPort()!=443)throw new IOException();
            String host=target.getHost();
            if(!Set.of("raw.githubusercontent.com","github.com","release-assets.githubusercontent.com","objects.githubusercontent.com").contains(host))throw new IOException();
            HttpsURLConnection c=(HttpsURLConnection)target.openConnection(Proxy.NO_PROXY);connection=c;c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(10000);c.setReadTimeout(10000);c.setUseCaches(false);c.setRequestProperty("Accept-Encoding","identity");
            int status=c.getResponseCode();
            if(status==200)return c.getInputStream();
            String next=c.getHeaderField("Location");c.disconnect();
            if(!Set.of(301,302,303,307,308).contains(status)||next==null)throw new IOException();
            target=new URL(target,next);
            if(target.getHost().equals("raw.githubusercontent.com"))throw new IOException();
        }throw new IOException();
    }
    private void finishConnection(){HttpsURLConnection c=connection;connection=null;if(c!=null)c.disconnect();}
    private void check()throws Exception {
        byte[] raw;try(InputStream in=open(FEED)){ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] b=new byte[4096];int n;while((n=in.read(b))!=-1){if(bytes.size()+n>65536)throw new IOException();bytes.write(b,0,n);}raw=bytes.toByteArray();}finally{finishConnection();}
        if(raw.length>65536)throw new IOException();
        JSONObject envelope=new JSONObject(new String(raw,StandardCharsets.UTF_8));
        if(!envelope.getString("schema").equals("cloud-stick-app-signature/v1"))throw new IOException();
        byte[] payload=Base64.getDecoder().decode(envelope.getString("payload"));
        verified(payload,Base64.getDecoder().decode(envelope.getString("signature")));
        JSONObject catalog=new JSONObject(new String(payload,StandardCharsets.UTF_8));
        int revision=catalog.getInt("revision"),minimum=context.getSharedPreferences("app-updates",0).getInt("revision",1);
        if(!catalog.getString("schema").equals("cloud-stick-app-catalog/v1")||revision<minimum)throw new IOException();
        JSONArray releases=catalog.getJSONArray("releases");JSONObject row=null;
        if(releases.length()<1||releases.length()>4)throw new IOException();
        for(int i=0;i<releases.length();i++)if(releases.getJSONObject(i).getString("platform").equals("android")){if(row!=null)throw new IOException();row=releases.getJSONObject(i);}
        if(row==null||!row.getString("tag").matches("[A-Za-z0-9][A-Za-z0-9._-]{0,95}")||!row.getString("asset").matches("(?:cloud-stick-|Cloud-Stick-)[A-Za-z0-9._-]{1,120}\\.apk")||!row.getString("sha256").matches("[0-9a-f]{64}")||!row.getString("version").matches("[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}(?:-[a-z0-9.-]{1,24})?")||row.getLong("size")<1024||row.getLong("size")>64*1024*1024||row.getInt("code")<1)throw new IOException();
        if(!context.getSharedPreferences("app-updates",0).edit().putInt("revision",revision).commit())throw new IOException();
        synchronized(this){release=row;offered=row.getString("version");phase=row.getInt("code")>BuildConfig.VERSION_CODE?"available":"current";}
    }
    private void download()throws Exception {
        JSONObject row; synchronized(this){row=release;total=row.getLong("size");}
        File part=new File(context.getFilesDir(),"update.part");
        try {
            MessageDigest digest=MessageDigest.getInstance("SHA-256");long count=0;
            try(InputStream in=open("https://github.com/rtariq2015/cloud-stick-pro/releases/download/"+row.getString("tag")+"/"+row.getString("asset"));FileOutputStream out=new FileOutputStream(part)){
                byte[] buffer=new byte[65536];int n;while((n=in.read(buffer))!=-1){count+=n;if(closed||count>row.getLong("size"))throw new IOException();out.write(buffer,0,n);digest.update(buffer,0,n);synchronized(this){done=count;}}
                out.getFD().sync();
            }finally{finishConnection();}
            if(count!=row.getLong("size")||!WakeProtocol.hex(digest.digest()).equals(row.getString("sha256")))throw new IOException();
            verifyApk(part,row);
            if(!part.renameTo(apk))throw new IOException();synchronized(this){phase="ready";}
        }finally{part.delete();}
    }
    private void verifyApk(File file,JSONObject row)throws Exception {
        var info=context.getPackageManager().getPackageArchiveInfo(file.getPath(),PackageManager.GET_SIGNING_CERTIFICATES);
        if(info==null||!info.packageName.equals(context.getPackageName())||info.getLongVersionCode()!=row.getInt("code")||info.getLongVersionCode()<=BuildConfig.VERSION_CODE||info.signingInfo==null)throw new IOException();
        var signers=info.signingInfo.getApkContentsSigners();
        if(signers.length!=1||!WakeProtocol.hex(MessageDigest.getInstance("SHA-256").digest(signers[0].toByteArray())).equals(SIGNER))throw new IOException();
    }
    synchronized File installation()throws Exception {
        if(closed||!phase.equals("ready")||release==null||!apk.isFile()||apk.length()!=release.getLong("size"))throw new IOException();
        MessageDigest digest=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(apk)){byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)digest.update(b,0,n);}
        if(!WakeProtocol.hex(digest.digest()).equals(release.getString("sha256")))throw new IOException();verifyApk(apk,release);return apk;
    }
    @Override public void close(){closed=true;finishConnection();executor.shutdownNow();}
}
