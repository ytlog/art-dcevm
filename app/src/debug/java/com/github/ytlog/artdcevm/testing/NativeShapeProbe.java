package com.github.ytlog.artdcevm.testing;
/** Mixed JNI register/return shapes across native/Java implementation changes. */
public final class NativeShapeProbe {
    private NativeShapeProbe() {}
    public static native void register(Class<?> cls);
    public static native void retain(Object object);
    public static native long wide(int mode, long x, double y, Object token);
    public static native Object reference(int mode, boolean statik, Object token);
    public static native void release();
}
