package com.github.ytlog.artdcevm.testing;

import android.os.Process;
import org.json.JSONObject;
import org.junit.Test;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeFalse;

public final class Arm32ReceiverCalibrationTest {
    @Test public void originalRunnableReceiverProof() throws Exception {
        assumeFalse("Requires an actual ARM32 process", Process.is64Bit());
        JSONObject first = new JSONObject(Arm32ReceiverProbe.run());
        assertTrue(first.toString(), first.getBoolean("passed"));
        assertTrue(first.toString(), first.getBoolean("hookInstalled"));
        assertTrue(first.toString(), first.getBoolean("receiverReady"));
        // Repeated real JNI dispatch uses the retained original trampoline.
        JSONObject repeated = new JSONObject(Arm32ReceiverProbe.run());
        assertTrue(repeated.toString(), repeated.getBoolean("passed"));
    }
}
