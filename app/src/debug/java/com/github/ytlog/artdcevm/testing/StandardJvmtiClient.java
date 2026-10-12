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
    /** Enable only this thread's standard JVMTI single-step event. Always disable in finally. */
    static native int setThreadSingleStep(boolean enabled);
    static native long singleStepCount();
}
