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
    private final Handler handler=new Handler(Looper.getMainLooper());
    private WebView web; private GattSession session; private final PermissionGate permissionGate=new PermissionGate();
    private final ExecutorService dashboardExecutor=new ThreadPoolExecutor(1,1,0L,TimeUnit.MILLISECONDS,new ArrayBlockingQueue<>(1));
    private Future<?> dashboardTask; private DashboardClient dashboardClient; private GattSession.Reply dashboardReply;
    private long dashboardEpoch;
    private String savedAddress(){return getSharedPreferences("dashboard",MODE_PRIVATE).getString("address","");}
    private void cancelDashboard(){
        ++dashboardEpoch;
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
    private boolean resumed; private long permissionEpoch;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
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
            Map<String,Object> request=StrictRequest.validated(raw); Object value=request.get("id");
            if(!(value instanceof Integer) || (int)value<1)throw new IllegalArgumentException();id=(int)value;
            String op=(String)request.get("op");int requestId=id;
            GattSession.Reply result=new GattSession.Reply(){
                public void ok(Map<String,Object> data){send(reply,requestId,data,null);}
                public void error(String code){send(reply,requestId,null,code);}
            };
            if(op.equals("dashboard_cancel")){cancelDashboard();result.ok(Map.of());return;}
            if(op.equals("disconnect")){session.disconnect(result);return;}
            if(!resumed){result.error("app_not_foreground");return;}
            if(op.equals("dashboard_address")){result.ok(Map.of("address",savedAddress()));return;}
            if(op.equals("dashboard_save")){
                String address=(String)request.get("address");if(!address.isEmpty())address=DashboardClient.address(address);
                cancelDashboard();
                if(!getSharedPreferences("dashboard",MODE_PRIVATE).edit().putString("address",address).commit()){result.error("dashboard_save_failed");return;}
                result.ok(Map.of("address",address));return;
            }
            if(op.equals("dashboard_status")){readDashboard(result);return;}
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
    @Override protected void onPause(){resumed=false;cancelDashboard();permissionGate.foreground(false);if(session!=null)session.foreground(false);if(web!=null)web.onPause();super.onPause();}
    @Override protected void onDestroy(){cancelDashboard();dashboardExecutor.shutdownNow();if(session!=null)session.foreground(false);permissionGate.cancel("cancelled");if(web!=null){web.destroy();web=null;}super.onDestroy();}
}
