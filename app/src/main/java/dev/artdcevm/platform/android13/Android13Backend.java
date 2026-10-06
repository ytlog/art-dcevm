package dev.artdcevm.platform.android13;

import android.content.Context;
import android.annotation.TargetApi;
import android.content.pm.ApplicationInfo;
import android.os.Debug;
import dev.artdcevm.api.PatchRequest;
import dev.artdcevm.api.PatchResult;
import dev.artdcevm.api.RuntimeBackend;
import dev.artdcevm.engine.NativeBridge;
import org.json.JSONObject;
import java.io.File;

/** API 33 attach and capability policy. Other Android releases must not reuse private assumptions. */
@TargetApi(33)
public final class Android13Backend implements RuntimeBackend {
    private final String sourceModulePath;
    public Android13Backend(Context context) throws Exception {
        if ((context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0)
            throw new SecurityException("This backend requires a debuggable app from process start");
        System.loadLibrary("artdcevm");
        // Installed APK paths contain '=' on this device, which attachJvmtiAgent rejects.
        // Let the supplied app ClassLoader resolve the library through its native search path.
        Debug.attachJvmtiAgent("libartdcevm.so", null, context.getClassLoader());
        sourceModulePath = new File(context.getApplicationInfo().nativeLibraryDir,
                "libartdcevm_source13.so").getAbsolutePath();
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
    @Override public synchronized PatchResult enableEvolution() {
        if (!new File(sourceModulePath).isFile())
            return new PatchResult(98, "Source module is not packaged; build with -PsourceEngine13");
        int code = NativeBridge.initializeEvolution(sourceModulePath, new SourceAbiProbe());
        return new PatchResult(code, NativeBridge.errorName(code));
    }
}
