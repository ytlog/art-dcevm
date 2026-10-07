package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.os.Debug;

/** Independent test Agent: class changes go through its own standard JVMTI environment. */
public final class StandardJvmtiClient {
    private static boolean attached;
    private static native int redefineNative(Class<?>[] classes, byte[][] dexes);

    private StandardJvmtiClient() {}

    public static synchronized void attach(Context context) throws Exception {
        if (attached) return;
        System.loadLibrary("artdcevm_client_test");
        Debug.attachJvmtiAgent("libartdcevm_client_test.so", null, context.getClassLoader());
        attached = true;
    }

    public static int redefine(Class<?>[] classes, byte[][] dexes) {
        if (!attached) throw new IllegalStateException("JVMTI test Agent is not attached");
        return redefineNative(classes, dexes);
    }
}
