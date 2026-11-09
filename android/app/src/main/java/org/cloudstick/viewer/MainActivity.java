package org.cloudstick.viewer;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.WindowManager;
import android.webkit.*;
import androidx.webkit.*;
import org.json.*;
import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;

/** Local asset UI; isolated public LAN status reader and authenticated Bluetooth history. */
public final class MainActivity extends Activity {
    private AppUpdates appUpdates;
    private WakeKeys wakeKeys;
    private WakeClient wakeClient;
    private Future<?> wakeTask;
    private GattSession.Reply wakeReply;
    private long wakeEpoch;
    private void cancelWake(){
        ++wakeEpoch;if(wakeClient!=null)wakeClient.close();if(wakeTask!=null)wakeTask.cancel(true);
        wakeTask=null;wakeClient=null;GattSession.Reply reply=wakeReply;wakeReply=null;if(reply!=null)reply.error("wake_cancelled");
    }
    private void wake(GattSession.Reply reply){
        if(wakeTask!=null){reply.error("wake_busy");return;}
        if(!wakeKeys.hasPairings()||savedAddress().isEmpty()){reply.ok(Map.of("state","first_pair"));return;}
        cancelDashboard();final long epoch=++wakeEpoch;final WakeClient client=new WakeClient();wakeClient=client;wakeReply=reply;
        final String address=savedAddress();
        try{wakeTask=dashboardExecutor.submit(()->{
            String device=null,error="wake_unavailable";
            try{device=client.wake(address,new WakeClient.Keys(){
                public String device(String id){return wakeKeys.device(id);}
                public byte[] sign(String id,String nonce)throws java.security.GeneralSecurityException{return wakeKeys.sign(id,nonce);}
            });}catch(WakeClient.Failure failure){error=failure.code;}catch(Exception ignored){}finally{client.close();}
            final String found=device,code=error;
            handler.post(()->{if(epoch!=wakeEpoch)return;wakeTask=null;wakeClient=null;wakeReply=null;if(!resumed){reply.error("app_not_foreground");return;}
                if(found!=null)reply.ok(Map.of("state","opening","device_id",found));else if(code.equals("wake_not_paired"))reply.ok(Map.of("state","first_pair"));else reply.error(code);});
        });}catch(RejectedExecutionException ignored){wakeTask=null;wakeClient=null;wakeReply=null;client.close();reply.error("wake_unavailable");}
    }
    private void wakeKey(GattSession.Reply reply){
        String device=session.readyDevice();if(device==null){reply.error("bluetooth_not_ready");return;}
        if(wakeTask!=null){reply.error("wake_busy");return;}final long epoch=++wakeEpoch,generation=session.generation();wakeReply=reply;
        try{wakeTask=dashboardExecutor.submit(()->{
            byte[] key=null;try{key=wakeKeys.publicKey(device);}catch(Exception ignored){}final byte[] publicKey=key;
            handler.post(()->{if(epoch!=wakeEpoch)return;wakeTask=null;wakeReply=null;
                if(!resumed||generation!=session.generation()||!device.equals(session.readyDevice())){reply.error("cancelled");return;}
                if(publicKey==null)reply.error("wake_key_unavailable");else reply.ok(Map.of("public_key",WakeProtocol.hex(publicKey)));});
        });}catch(RejectedExecutionException ignored){wakeTask=null;wakeReply=null;reply.error("wake_key_unavailable");}
    }
    private final Handler handler=new Handler(Looper.getMainLooper());
    private WebView web; private GattSession session; private final PermissionGate permissionGate=new PermissionGate();
    private final ExecutorService dashboardExecutor=new ThreadPoolExecutor(1,1,0L,TimeUnit.MILLISECONDS,new ArrayBlockingQueue<>(1));
    private LanDiscovery discovery;
    private Future<?> dashboardTask; private DashboardClient dashboardClient; private GattSession.Reply dashboardReply;
    private long dashboardEpoch;
    private String savedAddress(){return getSharedPreferences("dashboard",MODE_PRIVATE).getString("address","");}
    private void cancelDashboard(){
        ++dashboardEpoch;if(discovery!=null)discovery.close();discovery=null;
        if(dashboardClient!=null)dashboardClient.close();
        if(dashboardTask!=null)dashboardTask.cancel(true);
        dashboardTask=null;dashboardClient=null;
        GattSession.Reply reply=dashboardReply;dashboardReply=null;if(reply!=null)reply.error("dashboard_cancelled");
    }
    private void readDashboard(GattSession.Reply reply){
        if(dashboardTask!=null){reply.error("dashboard_busy");return;}
        final String address=savedAddress();
        try{DashboardClient.address(address);}catch(IllegalArgumentException e){reply.error("dashboard_address_required");return;}
        final long epoch=++dashboardEpoch;final DashboardClient client=new DashboardClient();
        dashboardClient=client;dashboardReply=reply;
        try{dashboardTask=dashboardExecutor.submit(()->{
            Map<String,Object> data=null;try{data=client.read(address);}catch(Exception ignored){}finally{client.close();}
            final Map<String,Object> status=data;
            handler.post(()->{if(epoch!=dashboardEpoch)return;dashboardTask=null;dashboardClient=null;dashboardReply=null;
                if(resumed&&status!=null)reply.ok(status);else reply.error("dashboard_unavailable");});
        });}catch(RejectedExecutionException e){dashboardClient=null;dashboardReply=null;client.close();reply.error("dashboard_busy");}
    }
    private void discoverAddress(GattSession.Reply reply){
        if(dashboardTask!=null||!session.state().equals("idle")){reply.error("dashboard_busy");return;}
        final long epoch=++dashboardEpoch;final LanDiscovery search=new LanDiscovery();discovery=search;dashboardReply=reply;
        try{dashboardTask=dashboardExecutor.submit(()->{
            java.util.List<String> found=null;try{found=search.find();}catch(Exception ignored){}finally{search.close();}
            final java.util.List<String> rows=found;
            handler.post(()->{if(epoch!=dashboardEpoch)return;discovery=null;dashboardTask=null;dashboardReply=null;
                if(resumed&&rows!=null)reply.ok(Map.of("addresses",rows));else reply.error("dashboard_unavailable");});
        });}catch(RejectedExecutionException ignored){search.close();discovery=null;dashboardTask=null;dashboardReply=null;reply.error("dashboard_busy");}
    }
    private boolean resumed; private long permissionEpoch;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        wakeKeys=new WakeKeys(this);
        appUpdates=new AppUpdates(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        AndroidBleDriver driver=new AndroidBleDriver(this,handler);
        session=new GattSession(driver,(r,ms)->handler.postDelayed(r,ms),this::event);driver.session=session;
        web=new WebView(this);setContentView(web);
        web.setImportantForAutofill(WebView.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);
        WebSettings settings=web.getSettings();settings.setJavaScriptEnabled(true);
        settings.setAllowFileAccess(false);settings.setAllowContentAccess(false);settings.setDomStorageEnabled(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);settings.setSupportMultipleWindows(false);
        settings.setGeolocationEnabled(false);settings.setMediaPlaybackRequiresUserGesture(true);
        CookieManager.getInstance().setAcceptCookie(false);CookieManager.getInstance().setAcceptThirdPartyCookies(web,false);
        WebView.setWebContentsDebuggingEnabled(false);
        WebViewAssetLoader assets=new WebViewAssetLoader.Builder()
            .addPathHandler("/assets/",new WebViewAssetLoader.AssetsPathHandler(this)).build();
        web.setWebViewClient(new WebViewClient(){
            @Override public WebResourceResponse shouldInterceptRequest(WebView view,WebResourceRequest request) {
                if(!request.getMethod().equals("GET") || !LocalPolicy.resource(request.getUrl().toString()))return blocked();
                WebResourceResponse response=assets.shouldInterceptRequest(request.getUrl());
                if(response==null)return blocked();
                String path=request.getUrl().getPath();
                response.setMimeType(path.endsWith(".mjs")?"text/javascript":path.endsWith(".css")?"text/css":"text/html");
                response.setEncoding("UTF-8");
                response.setResponseHeaders(Map.of("Content-Security-Policy","default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'none'; img-src 'none'; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
                    "X-Content-Type-Options","nosniff","Cache-Control","no-store","Referrer-Policy","no-referrer"));
                return response;
            }
            @Override public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest request) {return !request.isForMainFrame() || !LocalPolicy.navigation(request.getUrl().toString());}
            @Override public void onReceivedSslError(WebView view,android.webkit.SslErrorHandler response,android.net.http.SslError error) {response.cancel();}
            @Override public boolean onRenderProcessGone(WebView view,RenderProcessGoneDetail detail) {session.foreground(false);view.destroy();finish();return true;}
        });
        web.setWebChromeClient(new WebChromeClient(){
            @Override public void onPermissionRequest(PermissionRequest request){request.deny();}
            @Override public boolean onConsoleMessage(ConsoleMessage message){return true;}
            @Override public boolean onCreateWindow(WebView view,boolean dialog,boolean gesture,android.os.Message message){return false;}
        });
        web.setDownloadListener((a,b,c,d,e)->{});
        if(WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
            WebViewCompat.addWebMessageListener(web,"CloudStickNative",Set.of(LocalPolicy.ORIGIN),(view,message,origin,main,reply)->{
                if(!LocalPolicy.bridge(origin.toString(),main))return;
                receive(message.getData(),reply);
            });
            web.loadUrl(LocalPolicy.INDEX);
        }else{
            android.widget.TextView text=new android.widget.TextView(this);text.setText(R.string.webview_update);setContentView(text);
        }
    }
    private WebResourceResponse blocked() {return new WebResourceResponse("text/plain","UTF-8",403,"Blocked",Map.of("Cache-Control","no-store"),new ByteArrayInputStream(new byte[0]));}
    private void event(Map<String,Object> event) {
        if(web!=null)web.evaluateJavascript("globalThis.cloudStickNativeEvent?.("+new JSONObject(event)+")",null);
    }
    private void receive(String raw,JavaScriptReplyProxy reply) {
        int id=0;
        try {
            if(raw==null || raw.length()>4096)throw new IllegalArgumentException();
            // Correlate a well-formed request before semantic validation. Rejected
            // addresses/operations still need a bounded error reply to their caller.
            id=StrictRequest.requestId(raw);
            Map<String,Object> request=StrictRequest.validated(raw);
            String op=(String)request.get("op");int requestId=id;
            GattSession.Reply result=new GattSession.Reply(){
                public void ok(Map<String,Object> data){send(reply,requestId,data,null);}
                public void error(String code){send(reply,requestId,null,code);}
            };
            if(op.equals("dashboard_cancel")){cancelDashboard();result.ok(Map.of());return;}
            if(op.equals("disconnect")){cancelWake();session.disconnect(result);return;}
            if(!resumed){result.error("app_not_foreground");return;}
            if(op.equals("app_update_state")){result.ok(appUpdates.state());return;}
            if(op.equals("app_update_check")||op.equals("app_update_download")){result.ok(appUpdates.start(op.equals("app_update_download")));return;}
            if(op.equals("app_update_install")){
                if(!session.state().equals("idle")||wakeTask!=null){result.error("disconnect_first");return;}
                try{
                    appUpdates.installation();
                    if(!getPackageManager().canRequestPackageInstalls()){
                        startActivity(new android.content.Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,Uri.parse("package:"+getPackageName())));
                        result.error("app_update_permission_required");return;
                    }
                    android.content.Intent install=new android.content.Intent(android.content.Intent.ACTION_VIEW);
                    install.setDataAndType(Uri.parse("content://org.cloudstick.viewer.updates/update.apk"),"application/vnd.android.package-archive");
                    install.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);startActivity(install);result.ok(Map.of("installing",true));
                }catch(Exception ignored){result.error("app_update_unavailable");}return;
            }
            if(op.equals("dashboard_address")){result.ok(Map.of("address",savedAddress()));return;}
            if(op.equals("dashboard_save")){
                String address=(String)request.get("address");if(!address.isEmpty())address=DashboardClient.address(address);
                cancelDashboard();
                if(!getSharedPreferences("dashboard",MODE_PRIVATE).edit().putString("address",address).commit()){result.error("dashboard_save_failed");return;}
                result.ok(Map.of("address",address));return;
            }
            if(op.equals("dashboard_discover")){discoverAddress(result);return;}
            if(op.equals("dashboard_status")){readDashboard(result);return;}
            if(op.equals("wake")){if(!permitted()){result.error("bluetooth_permission_required");return;}wake(result);return;}
            if(op.equals("wake_key")){wakeKey(result);return;}
            if(op.equals("wake_remember")){
                String device=session.readyDevice();if(device==null){result.error("bluetooth_not_ready");return;}
                try{wakeKeys.remember(device,(String)request.get("wake_id"));result.ok(Map.of());}catch(Exception ignored){result.error("wake_key_unavailable");}return;
            }
            if(op.equals("permission")){permission(result);return;}
            if(!permitted()){result.error("bluetooth_permission_required");return;}
            switch(op){
                case "discover"->session.scan(result);
                case "connect"->{String target=(String)request.get("device_id");if(!target.matches("(?:[0-9A-F]{2}:){5}[0-9A-F]{2}"))throw new IllegalArgumentException();session.connect(target,result);}
                case "read"->{String uuid=strictUuid(request);session.read(uuid,result);}
                case "write"->{String uuid=strictUuid(request),hex=(String)request.get("hex");if(!hex.matches("(?:[0-9a-f]{2}){1,512}"))throw new IllegalArgumentException();byte[] bytes=new byte[hex.length()/2];for(int i=0;i<bytes.length;i++)bytes[i]=(byte)Integer.parseInt(hex.substring(i*2,i*2+2),16);session.write(uuid,bytes,result);Arrays.fill(bytes,(byte)0);}
                default->throw new IllegalArgumentException();
            }
        }catch(Exception e){send(reply,id,null,"invalid_request");}
    }
    private String strictUuid(Map<String,Object> request) {String uuid=(String)request.get("uuid");if(!GattSession.CHARACTERISTICS.contains(uuid))throw new IllegalArgumentException();return uuid;}
    private void send(JavaScriptReplyProxy reply,int id,Map<String,Object> result,String error) {
        if(!WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER))return;
        JSONObject response=new JSONObject();try{response.put("id",id);response.put("ok",error==null);if(error==null)response.put("result",new JSONObject(result));else response.put("error",error);reply.postMessage(response.toString());}catch(Exception ignored){}
    }
    private boolean permitted(){return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)==PackageManager.PERMISSION_GRANTED && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED;}
    private void permission(GattSession.Reply result) {
        if(permitted()){result.ok(Map.of());return;}
        if(!permissionGate.begin(result))return;long token=++permissionEpoch;
        try{requestPermissions(new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT},1);}
        catch(RuntimeException e){permissionGate.cancel("permission_unavailable");return;}
        handler.postDelayed(()->{if(token==permissionEpoch)permissionGate.cancel("permission_timeout");},60000);
    }
    @Override public void onRequestPermissionsResult(int request,String[] names,int[] results){super.onRequestPermissionsResult(request,names,results);if(request==1)permissionGate.result(permitted());}
    @Override protected void onResume(){super.onResume();resumed=true;if(session!=null)session.foreground(true);if(web!=null)web.onResume();permissionGate.foreground(true);}
    @Override protected void onPause(){resumed=false;cancelWake();cancelDashboard();permissionGate.foreground(false);if(session!=null)session.foreground(false);if(web!=null)web.onPause();super.onPause();}
    @Override protected void onDestroy(){if(appUpdates!=null)appUpdates.close();cancelWake();cancelDashboard();dashboardExecutor.shutdownNow();if(session!=null)session.foreground(false);permissionGate.cancel("cancelled");if(web!=null){web.destroy();web=null;}super.onDestroy();}
}
