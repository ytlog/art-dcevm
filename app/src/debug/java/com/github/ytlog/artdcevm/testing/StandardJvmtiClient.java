package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.os.Debug;

/** Java side of the independent user agent in standard_jvmti_client.cc. */
final class StandardJvmtiClient {
    private static boolean attached;

    private StandardJvmtiClient() {}

    static synchronized void attach(Context context) throws Exception {
        if (attached) return;
        System.loadLibrary("artdcevm_client_test");
        Debug.attachJvmtiAgent("libartdcevm_client_test.so", null, context.getClassLoader());
        attached = true;
    }

    /** Standard JVMTI RedefineClasses on the whole batch; returns the JVMTI error code. */
    static native int redefine(Class<?>[] classes, byte[][] dexes);
}
