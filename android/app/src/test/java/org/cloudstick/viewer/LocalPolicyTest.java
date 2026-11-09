package org.cloudstick.viewer;
import org.junit.Test;
import static org.junit.Assert.*;

public class LocalPolicyTest {
    @Test public void exactAssetsOnly(){for(String path:LocalPolicy.ASSETS)assertTrue(LocalPolicy.resource(LocalPolicy.ORIGIN+path));}
    @Test public void everyPackagedExecutableAssetIsExplicitlyAdmitted()throws Exception {
        java.nio.file.Path root=java.nio.file.Path.of("src/main/assets");assertTrue(java.nio.file.Files.isDirectory(root));
        java.util.Set<String> packaged=new java.util.HashSet<>();
        try(var paths=java.nio.file.Files.walk(root)){
            paths.filter(java.nio.file.Files::isRegularFile).forEach(path->{String name=root.relativize(path).toString().replace('\\','/');if(name.endsWith(".mjs")||name.endsWith(".html")||name.endsWith(".css"))packaged.add("/assets/"+name);});
        }
        assertEquals(packaged,LocalPolicy.ASSETS);
    }
    @Test public void hostileOriginsAndPathsRejected(){for(String url:new String[]{"http://appassets.androidplatform.net/assets/index.html","https://appassets.androidplatform.net:443/assets/index.html","https://appassets.androidplatform.net.evil/assets/index.html","file:///android_asset/index.html","https://evil.test/assets/index.html",LocalPolicy.INDEX+"?",LocalPolicy.INDEX+"?x=1",LocalPolicy.INDEX+"#x",LocalPolicy.ORIGIN+"/assets/../index.html",LocalPolicy.ORIGIN+"/assets/%69ndex.html",LocalPolicy.ORIGIN+"/assets/private.json"})assertFalse(url,LocalPolicy.resource(url));}
    @Test public void lanPagesNeverEnterTheWebViewOrBridge(){for(String url:new String[]{"http://192.168.1.42/","http://10.0.0.1/status","https://192.168.1.42/"}){assertFalse(LocalPolicy.resource(url));assertFalse(LocalPolicy.navigation(url));assertFalse(LocalPolicy.bridge(url,true));}}
    @Test public void navigationAndBridgeAreNarrow(){assertTrue(LocalPolicy.navigation(LocalPolicy.INDEX));assertFalse(LocalPolicy.navigation(LocalPolicy.ORIGIN+"/assets/app.mjs"));assertTrue(LocalPolicy.bridge(LocalPolicy.ORIGIN,true));assertFalse(LocalPolicy.bridge(LocalPolicy.ORIGIN,false));assertFalse(LocalPolicy.bridge("null",true));assertFalse(LocalPolicy.bridge(LocalPolicy.ORIGIN+"/",true));}
}
