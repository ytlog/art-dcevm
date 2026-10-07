package com.github.ytlog.artdcevm.api;

import android.content.Context;
import com.github.ytlog.artdcevm.engine.BackendRegistry;
import com.github.ytlog.artdcevm.internal.PatchResult;
import com.github.ytlog.artdcevm.internal.RuntimeBackend;
import org.json.JSONObject;

/** Attaches the agent; full initialization also routes standard RedefineClasses. */
public final class DcevmRuntime {
    private static RuntimeBackend backend;
    private static boolean evolutionInitialized;

    private DcevmRuntime() {}

    public static synchronized void initialize(Context context) throws Exception {
        if (evolutionInitialized) return;
        RuntimeBackend candidate = backend != null ? backend : BackendRegistry.open(context.getApplicationContext());
        PatchResult result = candidate.enableEvolution();
        if (!result.installed())
            throw new IllegalStateException("ART redefine initialization failed: " + result
                    + "; " + candidate.capabilities().optString("evolutionError"));
        backend = candidate;
        evolutionInitialized = true;
    }

    /** Attach for standard JVMTI method-body redefine and inspect available ART extensions. */
    public static synchronized JSONObject attachPublicJvmti(Context context) throws Exception {
        if (backend == null) backend = BackendRegistry.open(context.getApplicationContext());
        PatchResult routing = backend.enableStandardRedefineRouting();
        if (!routing.installed()) throw new IllegalStateException(
                "ART standard redefine routing failed: " + routing + "; "
                        + backend.capabilities().optString("evolutionError"));
        return backend.capabilities();
    }
}
