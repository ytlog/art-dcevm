package dev.artdcevm.api;

import android.content.Context;
import dev.artdcevm.engine.BackendRegistry;
import dev.artdcevm.internal.PatchResult;
import dev.artdcevm.internal.RuntimeBackend;

/** Installs ART's standard RedefineClasses routing before a user Agent redefines classes. */
public final class DcevmRuntime {
    private static RuntimeBackend backend;

    private DcevmRuntime() {}

    public static synchronized void initialize(Context context) throws Exception {
        if (backend != null) return;
        RuntimeBackend candidate = BackendRegistry.open(context.getApplicationContext());
        PatchResult result = candidate.enableEvolution();
        if (!result.installed())
            throw new IllegalStateException("ART redefine initialization failed: " + result
                    + "; " + candidate.capabilities().optString("evolutionError"));
        backend = candidate;
    }
}
