package com.github.ytlog.artdcevm.testing;

import android.util.Log;
import androidx.test.platform.app.InstrumentationRegistry;
import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.assertTrue;

/** Runs the same portable behavior suite on local and cloud Android devices. */
public final class PublicJvmtiCloudTest {
    @Test public void publicJvmtiBehavior() {
        JSONObject report = new PublicJvmtiSuite(
                InstrumentationRegistry.getInstrumentation().getTargetContext()).run();
        Log.i("ArtDcevmCloud", report.toString());
        assertTrue(report.toString(), report.optBoolean("passed"));
    }
}
