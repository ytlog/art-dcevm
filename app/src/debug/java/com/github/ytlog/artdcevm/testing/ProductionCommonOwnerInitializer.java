package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import com.github.ytlog.artdcevm.api.DcevmRuntime;
import org.json.JSONObject;

/** Selects the complete common process owner before any private source hooks. */
final class ProductionCommonOwnerInitializer {
    private ProductionCommonOwnerInitializer() {}

    static JSONObject initialize(Context context) throws Exception {
        JSONObject attached = DcevmRuntime.attachPublicJvmti(context);
        int result = NativeBridge.initializeCommonCapabilities();
        if (result != 0) {
            throw new IllegalStateException("Explicit whole common owner failed: " + result
                + "; " + NativeBridge.capabilities());
        }
        return attached;
    }
}
