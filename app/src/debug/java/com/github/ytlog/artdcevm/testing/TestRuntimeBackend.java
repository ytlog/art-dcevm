package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Debug;
import org.json.JSONObject;

/** Thin debug-only probe adapter. Backend selection is owned by native code. */
public final class TestRuntimeBackend {
    private final String id;

    public TestRuntimeBackend(Context context) throws Exception {
        if ((context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0)
            throw new SecurityException("JVMTI requires a debuggable app from process start");
        System.loadLibrary("artdcevm");
        Debug.attachJvmtiAgent("libartdcevm.so", null, context.getClassLoader());
        id = capabilities().getString("backend");
    }
    public String id() { return id; }
    public JSONObject capabilities() throws Exception { return new JSONObject(NativeBridge.capabilities()); }
    public PatchResult apply(PatchRequest request) {
        return result(NativeBridge.apply(request.operation.wireCode, request.targets(), request.dex()));
    }
    public JSONObject analyze(PatchRequest request) throws Exception {
        return new JSONObject(NativeBridge.analyze(request.targets(), request.dex()));
    }
    public PatchResult enableValidationHooks() { return result(NativeBridge.enableValidationHooks()); }
    public PatchResult enableStandardRedefineRouting() { return result(NativeBridge.enableStandardRedefineRouting()); }
    public PatchResult enableEvolution() { return result(NativeBridge.initializeEvolution()); }
    private static PatchResult result(int code) { return new PatchResult(code, NativeBridge.errorName(code)); }

    public static final class PatchRequest {
        public enum Operation {
            REDEFINE_BODY(0), REDEFINE_STRUCTURE(1), EVOLVE_SCHEMA(2);
            public final int wireCode;
            Operation(int wireCode) { this.wireCode = wireCode; }
        }
        public final Operation operation;
        private final Class<?>[] targets;
        private final byte[][] dex;

        public PatchRequest(Operation operation, Class<?>[] targets, byte[][] dex) {
            if (operation == null || targets == null || dex == null || targets.length == 0 || targets.length != dex.length)
                throw new IllegalArgumentException("A nonempty class/dex batch is required");
            this.operation = operation;
            this.targets = targets.clone();
            this.dex = new byte[dex.length][];
            for (int i = 0; i < dex.length; i++) {
                if (targets[i] == null || dex[i] == null || dex[i].length == 0) throw new IllegalArgumentException("Null/empty definition");
                this.dex[i] = dex[i].clone();
            }
        }
        public Class<?>[] targets() { return targets.clone(); }
        public byte[][] dex() {
            byte[][] copy = new byte[dex.length][];
            for (int i = 0; i < dex.length; i++) copy[i] = dex[i].clone();
            return copy;
        }
    }

    public static final class PatchResult {
        public final int code;
        public final String detail;
        public PatchResult(int code, String detail) { this.code = code; this.detail = detail; }
        public boolean installed() { return code == 0; }
        @Override public String toString() { return code + ": " + detail; }
    }
}
