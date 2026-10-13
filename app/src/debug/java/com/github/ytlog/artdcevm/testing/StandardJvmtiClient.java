package com.github.ytlog.artdcevm.testing;

import android.content.Context;
import android.os.Debug;
import java.lang.reflect.Method;

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

    /** Resolve IDs before redefinition; invocation deliberately retains those exact IDs. */
    static native void cacheMethodIds(Class<?> type);
    static native int callCachedMethod(Object receiver, int method);
    static native int callReflectedMethod(Method method, Object receiver, int kind);
    /** form 1 passes a jvalue array; form 2 passes an explicitly constructed va_list. */
    static native int callCachedMethodVariant(Object receiver, int method, int form);
    static native int callReflectedMethodVariant(Method method, Object receiver, int kind, int form);
    static native void lookupMethodId(Class<?> type, int method);
    /**
     * Resolve field IDs before redefinition: 0 = int i, 1 = long j, 2 = Object o,
     * 3 = static int si, 4 = static Object so. Object reads return 1 when non-null.
     */
    static native void cacheFieldIds(Class<?> type);
    static native long readCachedField(Object receiver, int field);
    static native void writeCachedField(Object receiver, int field, long value);
    static native long readReflectedField(java.lang.reflect.Field field, Object receiver, int kind);
    /** Whether a class with this JVMTI signature is loaded; never loads it. */
    static native boolean isLoaded(String signature);
    /** Enable only this thread's standard JVMTI single-step event. Always disable in finally. */
    static native int setThreadSingleStep(boolean enabled);
    static native long singleStepCount();
}
