package com.github.ytlog.artdcevm.platform.jvmti;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Build;
import android.os.Debug;
import com.github.ytlog.artdcevm.engine.NativeBridge;
import com.github.ytlog.artdcevm.internal.PatchRequest;
import com.github.ytlog.artdcevm.internal.PatchResult;
import com.github.ytlog.artdcevm.internal.RuntimeBackend;
import org.json.JSONObject;

/** Public JVMTI path for releases without a calibrated private ART backend. */
public final class PublicJvmtiBackend implements RuntimeBackend {
    public PublicJvmtiBackend(Context context) throws Exception {
        if ((context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0)
            throw new SecurityException("JVMTI requires a debuggable app from process start");
        System.loadLibrary("artdcevm");
        Debug.attachJvmtiAgent("libartdcevm.so", null, context.getClassLoader());
    }
    @Override public String id() { return "public-jvmti-api" + Build.VERSION.SDK_INT; }
    @Override public JSONObject capabilities() throws Exception { return new JSONObject(NativeBridge.capabilities()); }
    @Override public PatchResult apply(PatchRequest request) {
        int code = NativeBridge.apply(request.operation.wireCode, request.targets(), request.dex());
        return new PatchResult(code, NativeBridge.errorName(code));
    }
    @Override public JSONObject analyze(PatchRequest request) throws Exception {
        return new JSONObject(NativeBridge.analyze(request.targets(), request.dex()));
    }
    @Override public PatchResult enableValidationHooks() {
        return unavailable();
    }
    @Override public PatchResult enableEvolution() {
        return unavailable();
    }
    private static PatchResult unavailable() {
        return new PatchResult(98, "No calibrated source engine or private hooks for this ART build");
    }
}
