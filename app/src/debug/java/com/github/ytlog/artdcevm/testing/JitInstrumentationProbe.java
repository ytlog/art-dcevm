package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.os.Debug;
import org.json.JSONObject;

/** Debug-only ART 13 experiment; never writes instrumentation fields. */
final class JitInstrumentationProbe {
    private JitInstrumentationProbe() { }
    static native int install();
    static native long[] snapshot();
    static native int maybeRestore();
    static native int cycleJvmti();
    static native String blockers();

    static void load() { System.loadLibrary("artdcevm_jit_probe"); }

    static void attach(Context context) throws Exception {
        Debug.attachJvmtiAgent("libartdcevm_jit_probe.so", null, context.getClassLoader());
    }

    static JSONObject state() throws Exception {
        String[] keys = {"sourcePrefixRecognized", "beforeDeoptStubs", "afterDeoptStubs",
                "exitStubsInstalled", "beforeForceDeoptId", "afterForceDeoptId", "forceDeoptId",
                "instrumentationLevel", "deoptimizedMethodsEmpty", "restoreAttempts",
                "jvmtiEnabledStubs", "singleStepEvents"};
        long[] values = snapshot();
        JSONObject result = new JSONObject();
        for (int i = 0; i < keys.length; i++) result.put(keys[i], values[i]);
        result.put("restorationBlockers", blockers());
        return result;
    }
}
