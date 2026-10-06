package dev.artdcevm.engine;

import android.content.Context;
import android.os.Build;
import dev.artdcevm.api.RuntimeBackend;
import dev.artdcevm.platform.android13.Android13Backend;

/** The only OS-selection point. Implementations and their policies live in platform packages. */
public final class BackendRegistry {
    private BackendRegistry() {}
    public static RuntimeBackend open(Context context) throws Exception {
        if (Build.VERSION.SDK_INT == 33) return new Android13Backend(context);
        throw new UnsupportedOperationException("No backend implemented for API " + Build.VERSION.SDK_INT);
    }
}
