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

    /**
     * ART's com.android.art.classloader.add_to_dex_class_loader: append a DEX,
     * jar or APK file to the loader's path; returns the JVMTI error code.
     */
    static native int addToClassLoader(ClassLoader loader, String path);
    /** Standard JVMTI RedefineClasses on the whole batch; returns the JVMTI error code. */
    static native int redefine(Class<?>[] classes, byte[][] dexes);
    /** ART's com.android.art.class.structurally_redefine_classes extension on the batch. */
    static native int redefineStructurally(Class<?>[] classes, byte[][] dexes);

    /** Resolve IDs before redefinition; invocation deliberately retains those exact IDs. */
    static native void cacheMethodIds(Class<?> type);
    /** Capture interface lambda bridges taking exactly one interface receiver. */
    static native void cacheInterfaceStaticMethods(Class<?> type, Method[] methods);
    static native int callInterfaceStatic(Object receiver, int method);
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
    /** Resolve one constructor ID before redefinition; newObjectCached(int) reuses it. */
    static native void cacheConstructor(Class<?> type, String signature);
    static native Object newObjectCached(int value);
    /** Whether a class with this JVMTI signature is loaded; never loads it. */
    static native boolean isLoaded(String signature);
    /**
     * Replace the definition of the class with this internal name in every
     * ClassFileLoadHook until cleared with nulls; returns the JVMTI error code.
     */
    static native int setClassFileReplacement(String name, byte[] dex);
    static native long classFileReplacements();
    /** Load one named class per matching hook, after the engine's snapshot. */
    static native int setClassFileLoads(String target, ClassLoader loader, String[] names);
    /** Retain exactly these JNI IDs across successful and rejected interface batches. */
    static native void cacheInterfaceDispatch(int slot, Method method);
    static native int callInterfaceDispatchInt(Object receiver, int slot);
    static native String callInterfaceDispatchString(Object receiver, int slot);
    /** Enable only this thread's standard JVMTI single-step event. Always disable in finally. */
    static native int setThreadSingleStep(boolean enabled);
    static native long singleStepCount();
}
