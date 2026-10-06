package dev.artdcevm.testing;

/** Cached JNI static method IDs across a visibility change. */
public final class StaticVisibilityProbe {
    private StaticVisibilityProbe() {}
    public static native boolean retain(Class<?> type);
    public static native int invoke(int mode, boolean fresh);
    public static native void release();
}
