package com.github.ytlog.artdcevm.fixture;

/** Opaque JNI ID and local-reference ownership shared by ART/JBR probes. */
public final class JniFrameProbe {
    private JniFrameProbe() {}
    public static native int cache(Class<?> owner);
    public static native boolean prepareThreadPolicy();
    public static native int invoke(Object receiver, boolean nonvirtual, int form, boolean holdNative);
}
