package com.github.ytlog.artdcevm.api;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Debug;
import com.github.ytlog.artdcevm.internal.RetiredField;
import com.github.ytlog.artdcevm.internal.RetiredStaticField;
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
        // Class literals do not initialize RetiredStaticField.
        int code = nativeInitialize(RetiredField.class, RetiredStaticField.class);
        if (code != 0) {
            throw new IllegalStateException("ART DCEVM initialization failed: " + nativeErrorName(code)
                    + " (" + code + "); " + capabilities().optString("diagnostic"));
        }
        initialized = true;
    }

    /**
     * Supported change categories, probe diagnostics and the last rejection reason.
     * Interface targets support default/static/private body edits, new public abstract,
     * default and static methods and private instance methods, and deletion or signature
     * changes of static/private methods, subject to transaction checks.
     *
     * <p>Reading this report does not run probes. Runtime declarations are probed during
     * interface transactions; the additional IfTable dispatch probes run for abstract
     * additions. {@code interfaceRedefinition} stays false until the runtime probes pass.
     * {@code interfaceAbstractMethodAddition} also requires verified dispatch probes and
     * the required link-error services. A false value while probes are
     * {@code unverified} does not mean unsupported and should not block the first transaction.
     *
     * <p>{@code interfaceRuntime} and {@code interfaceAbstractDispatch} describe the
     * runtime and dispatch-accessor probe states, respectively:
     * <ul>
     * <li>{@code unavailable} (optionally with a reason): the named facility is unavailable.</li>
     * <li>{@code disabled: reason}: the interface runtime has been disabled, for example
     * because the version, required services or runtime declarations do not match.</li>
     * <li>{@code unverified: ...}: transaction-time probes have not completed.</li>
     * <li>{@code verified: ...}: the relevant probes have passed; individual edits
     * still require transaction checks.</li>
     * <li>{@code rejected: reason}: dispatch-accessor probes or required-service checks
     * failed. This state is reported by {@code interfaceAbstractDispatch}.</li>
     * </ul>
     *
     * <p>When {@code interfaceRuntime} is unavailable or disabled, interface targets
     * reject with JVMTI {@code NOT_AVAILABLE} (98). If only the dispatch accessor is
     * unavailable or rejected, abstract additions reject with {@code METHOD_ADDED} (63).
     * The actual transaction result and {@code lastRejection} are the final authority.
     *
     * @return the current report, or an empty object before the library is attached
     */
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

    private static native int nativeInitialize(Class<?> retiredField, Class<?> retiredStaticField);
    private static native String nativeCapabilities();
    private static native String nativeErrorName(int code);
}
