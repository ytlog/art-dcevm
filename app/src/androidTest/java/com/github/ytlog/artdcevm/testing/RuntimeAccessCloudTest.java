package com.github.ytlog.artdcevm.testing;
import android.util.Log;
import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.assertTrue;
public final class RuntimeAccessCloudTest {
    @Test public void ownedJniAccess() {
        JSONObject report=new RuntimeAccessSuite().run(androidx.test.platform.app.InstrumentationRegistry
                .getInstrumentation().getTargetContext().getApplicationInfo().sourceDir);
        Log.i("ArtDcevmRuntimeAccess",report.toString());
        assertTrue(report.toString(),report.optBoolean("passed"));
    }
}
