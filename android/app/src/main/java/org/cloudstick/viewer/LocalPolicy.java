package org.cloudstick.viewer;
import java.net.URI;
import java.util.Set;

/** Pure origin/path policy shared by interception and navigation tests. */
final class LocalPolicy {
    static final String ORIGIN="https://appassets.androidplatform.net";
    static final String INDEX=ORIGIN+"/assets/index.html";
    static final Set<String> ASSETS=Set.of("/assets/index.html","/assets/style.css","/assets/app.mjs",
        "/assets/native.mjs","/assets/shared/connection.mjs","/assets/shared/security2.mjs");
    static boolean resource(String url) {
        try {
            URI uri=new URI(url);
            return uri.getScheme().equals("https") && "appassets.androidplatform.net".equals(uri.getRawAuthority())
                && uri.getRawQuery()==null && uri.getRawFragment()==null && ASSETS.contains(uri.getRawPath());
        }catch(Exception e){return false;}
    }
    static boolean navigation(String url) {return INDEX.equals(url);}
    static boolean bridge(String origin,boolean mainFrame) {return mainFrame && ORIGIN.equals(origin);}
}
