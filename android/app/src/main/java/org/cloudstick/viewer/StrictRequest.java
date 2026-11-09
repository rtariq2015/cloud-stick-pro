package org.cloudstick.viewer;
import java.util.*;

/** Small JSON subset: flat ASCII names/strings and positive int IDs.
 * Only canonical ops/UUIDs/addresses/hex cross the bridge, never credential text. */
final class StrictRequest {
    private final String text; private int at;
    private StrictRequest(String text){this.text=text;}
    static int requestId(String text) {
        Object value=parse(text).get("id");
        if(!(value instanceof Integer) || (int)value<1)throw new IllegalArgumentException();
        return (int)value;
    }
    static Map<String,Object> validated(String text) {
        Map<String,Object> request=parse(text);
        if(!(request.get("id") instanceof Integer) || (int)request.get("id")<1 || !(request.get("op") instanceof String))throw new IllegalArgumentException();
        String op=(String)request.get("op");
        Set<String> allowed=switch(op){
            case "app_update_check","app_update_state","app_update_download","app_update_install","dashboard_discover","dashboard_address","dashboard_status","dashboard_cancel","discover","disconnect","permission","wake","wake_key"->Set.of("id","op");
            case "wake_remember"->Set.of("id","op","wake_id");
            case "dashboard_save"->Set.of("id","op","address");
            case "connect"->Set.of("id","op","device_id");
            case "read"->Set.of("id","op","uuid");
            case "write"->Set.of("id","op","uuid","hex");
            default->throw new IllegalArgumentException();
        };
        if(!request.keySet().equals(allowed))throw new IllegalArgumentException();
        if(op.equals("connect") && (!(request.get("device_id") instanceof String) || !((String)request.get("device_id")).matches("(?:[0-9A-F]{2}:){5}[0-9A-F]{2}")))throw new IllegalArgumentException();
        if((op.equals("read") || op.equals("write")) && !GattSession.CHARACTERISTICS.contains(request.get("uuid")))throw new IllegalArgumentException();
        if(op.equals("write") && (!(request.get("hex") instanceof String) || !((String)request.get("hex")).matches("(?:[0-9a-f]{2}){1,512}")))throw new IllegalArgumentException();
        if(op.equals("dashboard_save")){Object address=request.get("address");if(!(address instanceof String))throw new IllegalArgumentException();if(!address.equals(""))DashboardClient.address((String)address);}
        if(op.equals("wake_remember")&&(!(request.get("wake_id") instanceof String)||!((String)request.get("wake_id")).matches("[0-9a-f]{32}")))throw new IllegalArgumentException();
        return request;
    }
    static Map<String,Object> parse(String text) {
        if(text==null || text.length()>4096)throw new IllegalArgumentException();
        StrictRequest parser=new StrictRequest(text);Map<String,Object> result=new HashMap<>();
        parser.expect('{');
        if(parser.peek()!='}')while(true){
            String key=parser.string();parser.expect(':');Object value;
            if(parser.peek()=='"')value=parser.string();
            else {int start=parser.at;while(parser.at<text.length() && text.charAt(parser.at)>='0' && text.charAt(parser.at)<='9')parser.at++;
                String number=text.substring(start,parser.at);if(!number.matches("[1-9][0-9]{0,9}"))throw new IllegalArgumentException();value=Integer.valueOf(number);}
            if(result.putIfAbsent(key,value)!=null)throw new IllegalArgumentException();
            if(parser.peek()=='}')break;parser.expect(',');
        }
        parser.expect('}');if(parser.peek()!=0)throw new IllegalArgumentException();return result;
    }
    private char peek(){while(at<text.length() && " \r\n\t".indexOf(text.charAt(at))>=0)at++;return at==text.length()?0:text.charAt(at);}
    private void expect(char value){if(peek()!=value)throw new IllegalArgumentException();at++;}
    private String string(){expect('"');int start=at;while(at<text.length() && text.charAt(at)!='"'){
        char c=text.charAt(at++);if(c<32 || c>126 || c=='\\')throw new IllegalArgumentException();
    }if(at>=text.length())throw new IllegalArgumentException();String result=text.substring(start,at);at++;return result;}
}
