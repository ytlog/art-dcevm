package dev.artdcevm.testing;
/** Retains IDs from two incompatible receiver/storage generations for conformance. */
public final class StorageProbe {
    public static native int gcStress(int operation);
    private StorageProbe() {}
    public static native void registerNative(Class<?> cls, boolean flipped);
    public static native void retain(int generation, Object object, boolean flipped);
    public static native long field(int generation, int index, boolean write, Object token);
    public static native long invoke(int generation, int kind, int mode);
    public static native int breakpoint(java.lang.reflect.Method method, boolean enable);
    public static native int breakpointHits();
    public static native void release();
}
