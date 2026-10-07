package com.github.ytlog.artdcevm.platform.android13;

import android.content.Context;
import android.annotation.TargetApi;
import android.content.pm.ApplicationInfo;
import android.os.Debug;
import com.github.ytlog.artdcevm.internal.PatchRequest;
import com.github.ytlog.artdcevm.internal.PatchResult;
import com.github.ytlog.artdcevm.internal.RuntimeBackend;
import com.github.ytlog.artdcevm.engine.NativeBridge;
import org.json.JSONObject;

/** API 33 attach and capability policy. Other Android releases must not reuse private assumptions. */
@TargetApi(33)
public final class Android13Backend implements RuntimeBackend {
    public Android13Backend(Context context) throws Exception {
        if ((context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0)
            throw new SecurityException("This backend requires a debuggable app from process start");
        System.loadLibrary("artdcevm");
        // Installed APK paths contain '=' on this device, which attachJvmtiAgent rejects.
        // Let the supplied app ClassLoader resolve the library through its native search path.
        Debug.attachJvmtiAgent("libartdcevm.so", null, context.getClassLoader());
    }
    @Override public String id() { return "android13-arm64"; }
    @Override public JSONObject capabilities() throws Exception { return new JSONObject(NativeBridge.capabilities()); }
    @Override public synchronized PatchResult apply(PatchRequest request) {
        int code = NativeBridge.apply(request.operation.wireCode, request.targets(), request.dex());
        return new PatchResult(code, NativeBridge.errorName(code));
    }
    @Override public synchronized JSONObject analyze(PatchRequest request) throws Exception {
        return new JSONObject(NativeBridge.analyze(request.targets(), request.dex()));
    }
    @Override public synchronized PatchResult enableValidationHooks() {
        int code = NativeBridge.enableValidationHooks();
        return new PatchResult(code, NativeBridge.errorName(code));
    }
    @Override public synchronized PatchResult enableStandardRedefineRouting() {
        int code = NativeBridge.enableStandardRedefineRouting();
        return new PatchResult(code, NativeBridge.errorName(code));
    }
    @Override public synchronized PatchResult enableEvolution() {
        int code = NativeBridge.initializeEvolution(new EvolutionCalibration());
        return new PatchResult(code, NativeBridge.errorName(code));
    }
}
