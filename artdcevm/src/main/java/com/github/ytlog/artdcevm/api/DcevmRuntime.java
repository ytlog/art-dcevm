package com.github.ytlog.artdcevm.api;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Debug;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Entry point of the structural redefinition engine.
 *
 * <p>After {@link #initialize(Context)} succeeds, the standard JVMTI
 * {@code RedefineClasses} of any agent in this process accepts the changes
 * listed by {@link #capabilities()}; other changes are rejected with a JVMTI
 * error and leave the loaded classes unchanged.
 */
public final class DcevmRuntime {
    private static boolean attached;
    private static boolean initialized;

    private DcevmRuntime() {}

    /**
     * Loads the library, attaches it as a JVMTI agent and installs the engine.
     * Repeated calls after success return immediately.
     *
     * @throws SecurityException if the app was not debuggable from process start
     * @throws IllegalStateException if the engine cannot be installed on this runtime
     */
    public static synchronized void initialize(Context context) throws Exception {
        if (initialized) return;
        attach(context);
        int code = nativeInitialize();
        if (code != 0) {
            throw new IllegalStateException("ART DCEVM initialization failed: " + nativeErrorName(code)
                    + " (" + code + "); " + capabilities().optString("diagnostic"));
        }
        initialized = true;
    }

    /** Supported change categories, diagnostics and the last rejection reason. */
    public static synchronized JSONObject capabilities() {
        if (!attached) return new JSONObject();
        try {
            return new JSONObject(nativeCapabilities());
        } catch (JSONException e) {
            throw new IllegalStateException("Malformed capability report", e);
        }
    }

    private static void attach(Context context) throws Exception {
        if (attached) return;
        Context application = context.getApplicationContext();
        if ((application.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0)
            throw new SecurityException("JVMTI requires an app that is debuggable from process start");
        System.loadLibrary("artdcevm");
        // The class loader resolves the library name, so install paths containing '=' work.
        Debug.attachJvmtiAgent("libartdcevm.so", null, application.getClassLoader());
        attached = true;
    }

    private static native int nativeInitialize();
    private static native String nativeCapabilities();
    private static native String nativeErrorName(int code);
}
