package org.cloudstick.viewer;
import org.junit.Test;
import static org.junit.Assert.*;

public class LocalPolicyTest {
    @Test public void exactAssetsOnly(){for(String path:LocalPolicy.ASSETS)assertTrue(LocalPolicy.resource(LocalPolicy.ORIGIN+path));}
    @Test public void hostileOriginsAndPathsRejected(){for(String url:new String[]{"http://appassets.androidplatform.net/assets/index.html","https://appassets.androidplatform.net:443/assets/index.html","https://appassets.androidplatform.net.evil/assets/index.html","file:///android_asset/index.html","https://evil.test/assets/index.html",LocalPolicy.INDEX+"?",LocalPolicy.INDEX+"?x=1",LocalPolicy.INDEX+"#x",LocalPolicy.ORIGIN+"/assets/../index.html",LocalPolicy.ORIGIN+"/assets/%69ndex.html",LocalPolicy.ORIGIN+"/assets/private.json"})assertFalse(url,LocalPolicy.resource(url));}
    @Test public void navigationAndBridgeAreNarrow(){assertTrue(LocalPolicy.navigation(LocalPolicy.INDEX));assertFalse(LocalPolicy.navigation(LocalPolicy.ORIGIN+"/assets/app.mjs"));assertTrue(LocalPolicy.bridge(LocalPolicy.ORIGIN,true));assertFalse(LocalPolicy.bridge(LocalPolicy.ORIGIN,false));assertFalse(LocalPolicy.bridge("null",true));assertFalse(LocalPolicy.bridge(LocalPolicy.ORIGIN+"/",true));}
}
