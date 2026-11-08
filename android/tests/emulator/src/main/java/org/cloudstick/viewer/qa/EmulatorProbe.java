/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Cloud Stick Pro contributors
 */
package org.cloudstick.viewer.qa;
import android.app.*;
import android.os.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.view.*;
import android.webkit.*;
import android.accessibilityservice.AccessibilityService;
import android.view.accessibility.AccessibilityNodeInfo;
import org.json.*;
import java.util.*;
import java.util.concurrent.*;
import java.io.*;

/** Local signed instrumentation of the unmodified release APK, emulator only. */
public final class EmulatorProbe extends Instrumentation {
 private Bundle args; private Activity activity; private WebView web; private JSONArray checks=new JSONArray();
 @Override public void onCreate(Bundle values){super.onCreate(values);args=values;start();}
 private WebView find(View view){if(view instanceof WebView)return (WebView)view;if(view instanceof ViewGroup){ViewGroup group=(ViewGroup)view;for(int i=0;i<group.getChildCount();i++){WebView child=find(group.getChildAt(i));if(child!=null)return child;}}return null;}
 private String eval(String script)throws Exception{CountDownLatch latch=new CountDownLatch(1);String[] result={null};runOnMainSync(()->web.evaluateJavascript(script,value->{result[0]=value;latch.countDown();}));if(!latch.await(10,TimeUnit.SECONDS))throw new Exception("JavaScript callback deadline");return result[0];}
 private void check(String name,boolean ok)throws Exception{checks.put(new JSONObject().put("name",name).put("passed",ok));if(!ok)throw new Exception(name);}
 private boolean condition(String script)throws Exception{return "true".equals(eval("!!("+script+")"));}
 private void waitFor(String script,int seconds)throws Exception{long end=System.nanoTime()+seconds*1000000000L;while(System.nanoTime()<end){if(condition(script))return;Thread.sleep(150);}throw new Exception("Condition deadline: "+script.replaceAll("[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+","IP"));}
 private String text(String id)throws Exception{return (String)new JSONTokener(eval("document.getElementById("+JSONObject.quote(id)+").textContent")).nextValue();}
 private void click(String id)throws Exception{eval("document.getElementById("+JSONObject.quote(id)+").click()");}
 private void launch()throws Exception{activity=startActivitySync(new Intent().setClassName("org.cloudstick.viewer","org.cloudstick.viewer.MainActivity").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));runOnMainSync(()->web=find(activity.getWindow().getDecorView()));if(web==null)throw new Exception("WebView missing");waitFor("document.readyState==='complete'&&!!document.getElementById('dashboard-address')&&typeof CloudStickNative==='object'",15);Thread.sleep(500);}
 private void shell(String command)throws Exception{try(InputStream in=new ParcelFileDescriptor.AutoCloseInputStream(getUiAutomation().executeShellCommand(command))){while(in.read()!=-1){}}}
 private void choosePermission(String value)throws Exception{
  long end=System.nanoTime()+10000000000L;
  while(System.nanoTime()<end){AccessibilityNodeInfo root=getUiAutomation().getRootInActiveWindow();if(root!=null){for(AccessibilityNodeInfo node:root.findAccessibilityNodeInfosByText(value)){if(value.equals(String.valueOf(node.getText()))&&node.isClickable()&&node.performAction(AccessibilityNodeInfo.ACTION_CLICK))return;}}Thread.sleep(150);}throw new Exception("Permission dialog action unavailable");
 }
 private void runSuite(String address)throws Exception{
  check("local_HTTPS_origin",condition("location.href==='https://appassets.androidplatform.net/assets/index.html'&&isSecureContext"));
  check("first_launch_has_no_private_results",condition("document.getElementById('private-pane').hidden&&document.getElementById('result').hidden"));
  check("no_Bluetooth_permission_needed_for_home",getTargetContext().checkSelfPermission("android.permission.BLUETOOTH_SCAN")==PackageManager.PERMISSION_DENIED&&getTargetContext().checkSelfPermission("android.permission.BLUETOOTH_CONNECT")==PackageManager.PERMISSION_DENIED);
  check("first_launch_requests_only_an_address",condition("document.getElementById('address-details').open&&document.getElementById('dashboard-data').hidden"));
  check("phone_width_has_no_horizontal_overflow",condition("document.documentElement.scrollWidth<=innerWidth"));
  check("screenshot_protection_retained",(activity.getWindow().getAttributes().flags&WindowManager.LayoutParams.FLAG_SECURE)!=0);
  eval("document.getElementById('dashboard-address').value="+JSONObject.quote(address)+";document.getElementById('address-form').requestSubmit()");
  waitFor("document.getElementById('dashboard-badge').textContent==='Live over Wi-Fi'",20);
  check("real_stick_native_HTTP_and_UI",text("live-firmware").equals("0.4.6-cache-dev")&&!text("live-verified").equals("—"));
  check("real_stick_backups_not_paused_by_viewing",!text("live-title").equals("Backups paused"));
  check("saved_address_matches_selected_stick",text("saved-address").equals(address));
  check("inactive_transfer_progress_hidden",condition("document.getElementById('live-progress').hidden"));
  eval("window.__qaCspBlocked=false;fetch('http://"+address+"/status').then(()=>{},()=>{window.__qaCspBlocked=true;})");waitFor("window.__qaCspBlocked===true",5);
  check("renderer_network_blocked_while_native_reader_works",true);
  eval("document.getElementById('dashboard-address').value='8.8.8.8';document.getElementById('address-form').requestSubmit()");waitFor("document.getElementById('address-message').textContent.includes('local IP')",5);
  check("public_IP_rejected_and_saved_address_retained",text("saved-address").equals(address));
  waitFor("document.getElementById('dashboard-badge').textContent==='Live over Wi-Fi'",10);
  try{
   shell("cmd connectivity airplane-mode enable");waitFor("document.getElementById('dashboard-badge').textContent==='Not connected'",20);
   check("offline_readings_marked_old",text("live-updated").contains("previous read"));
   check("offline_transfer_progress_hidden",condition("document.getElementById('live-progress').hidden"));
  }finally{shell("cmd connectivity airplane-mode disable");shell("svc wifi enable");shell("svc data enable");}
  waitFor("document.getElementById('dashboard-badge').textContent==='Live over Wi-Fi'",30);check("network_restoration_reconnects",true);
  getUiAutomation().performGlobalAction(AccessibilityService.GLOBAL_ACTION_HOME);Thread.sleep(700);
  java.lang.reflect.Field resumed=activity.getClass().getDeclaredField("resumed"),task=activity.getClass().getDeclaredField("dashboardTask");resumed.setAccessible(true);task.setAccessible(true);
  check("background_cancels_native_dashboard_work",!resumed.getBoolean(activity)&&task.get(activity)==null);
  runOnMainSync(()->activity.finish());launch();waitFor("document.getElementById('dashboard-badge').textContent==='Live over Wi-Fi'",20);
  check("activity_restart_keeps_saved_address_and_reconnects",text("saved-address").equals(address));
  click("private-tab");waitFor("!document.getElementById('private-pane').hidden",5);
  check("private_history_has_separate_login",condition("!document.getElementById('login').hidden&&document.getElementById('result').hidden"));
  eval("document.getElementById('username').value='public-test-user';document.getElementById('password').value='PUBLIC-NOT-A-REAL-PASSWORD';document.getElementById('login-form').requestSubmit()");
  choosePermission("Don’t allow");waitFor("document.getElementById('message').textContent.includes('Nearby devices')",10);
  check("denied_permission_explained_without_private_results",condition("document.getElementById('result').hidden&&document.getElementById('password').value===''") );
  click("dashboard-tab");waitFor("!document.getElementById('dashboard-pane').hidden&&document.getElementById('dashboard-badge').textContent==='Live over Wi-Fi'",20);
  check("return_waits_for_Bluetooth_close_then_reconnects",text("connection-badge").equals("Bluetooth closed"));
  check("dashboard_home_still_works_without_Bluetooth_permission",getTargetContext().checkSelfPermission("android.permission.BLUETOOTH_SCAN")==PackageManager.PERMISSION_DENIED);
 }
 private void testBluetooth(String address)throws Exception{
  waitFor("document.getElementById('dashboard-badge').textContent==='Live over Wi-Fi'",20);
  click("private-tab");waitFor("!document.getElementById('private-pane').hidden",5);
  eval("window.__qaDeviceCount=-1;const previous=CloudStickNative.onmessage;CloudStickNative.onmessage=event=>{try{const value=JSON.parse(event.data);if(value.ok&&Array.isArray(value.result?.devices))window.__qaDeviceCount=value.result.devices.length;}catch{}previous(event);};");
  eval("document.getElementById('username').value='public-test-user';document.getElementById('password').value='PUBLIC-NOT-A-REAL-PASSWORD';document.getElementById('login-form').requestSubmit()");
  choosePermission("Allow");
  check("Android_Nearby_devices_permission_granted",getTargetContext().checkSelfPermission("android.permission.BLUETOOTH_SCAN")==PackageManager.PERMISSION_GRANTED&&getTargetContext().checkSelfPermission("android.permission.BLUETOOTH_CONNECT")==PackageManager.PERMISSION_GRANTED);
  waitFor("window.__qaDeviceCount===0&&document.getElementById('connection-badge').textContent==='Bluetooth closed'",20);
  check("real_Android_BLE_scan_completes_with_virtual_controller",true);
  check("empty_scan_explains_next_step_and_hides_private_results",condition("document.getElementById('result').hidden&&document.getElementById('password').value===''") );
  click("dashboard-tab");waitFor("document.getElementById('dashboard-badge').textContent==='Live over Wi-Fi'",20);
  check("BLE_scan_cleanup_returns_to_live_dashboard",text("connection-badge").equals("Bluetooth closed"));
 }
 @Override public void onStart(){Bundle report=new Bundle();JSONObject data=new JSONObject();boolean ok=false;
 try{
  if(!Build.HARDWARE.equals("ranchu")&&!Build.HARDWARE.equals("goldfish"))throw new Exception("This test probe requires an Android emulator");
  String address=args.getString("address","");String phase=args.getString("phase","suite");
  if(!address.matches("(?:0|[1-9][0-9]{0,2})(?:\\.(?:0|[1-9][0-9]{0,2})){3}"))throw new Exception("Private IPv4 test address required");
  String[] parts=address.split("\\.");int[] ip=new int[4];for(int i=0;i<4;i++){ip[i]=Integer.parseInt(parts[i]);if(ip[i]>255)throw new Exception("Invalid test address");}
  if(!(ip[0]==10||ip[0]==192&&ip[1]==168||ip[0]==172&&ip[1]>=16&&ip[1]<=31))throw new Exception("Private IPv4 test address required");
  if(!Arrays.asList("suite","persist","bluetooth").contains(phase))throw new Exception("Unknown test phase");
  launch();
  if(phase.equals("suite"))runSuite(address);
  else if(phase.equals("bluetooth"))testBluetooth(address);
  else if(phase.equals("persist")){waitFor("document.getElementById('dashboard-badge').textContent==='Live over Wi-Fi'",20);check("full_process_restart_retains_address",text("saved-address").equals(address));}
  ok=true;
 }catch(Exception error){try{data.put("failure",error.getMessage());data.put("current_public_badge",text("dashboard-badge"));data.put("current_message",text("message"));}catch(Exception ignored){}}
 try{data.put("passed",ok);data.put("checks",checks);data.put("android_api",Build.VERSION.SDK_INT);data.put("selected_LAN_endpoint_read",ok);data.put("physical_phone",false);}catch(Exception ignored){}
 report.putString("report",data.toString());finish(ok?Activity.RESULT_OK:Activity.RESULT_CANCELED,report);
 }
}
