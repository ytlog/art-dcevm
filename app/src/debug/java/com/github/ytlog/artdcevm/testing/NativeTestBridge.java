package com.github.ytlog.artdcevm.testing;

/** Conformance probes, separate from the runtime API. */
public final class NativeTestBridge {
    private NativeTestBridge() {}
    public static native void retainFixture(Object instance);
    public static native int[] inspectRetainedFixture();
    public static native void releaseFixture();
    public static native void retainRemovalMethod(Object instance);
    public static native int invokeRemovalMethod(int mode);
    public static native long invokeSignatureMethod(Object instance, long argument);
    public static native void retainFields(Object instance);
    public static native double accessField(int index, boolean write);
    public static native long accessChangedLongField(Object instance, long value);
    public static native void registerSpecialNatives(Class<?> cls);
    public static native void registerKeptNative(Class<?> cls);
    public static native void blockKeptNative(boolean block);
    public static native void retainSpecialMembers(Object instance);
    public static native Object createSpecial(int mode, int value);
    public static native Object createChangedSpecial(int mode, long value);
    public static native long invokeSpecialNative(int mode);
    public static native boolean waitSpecialNative();
    public static native void releaseSpecialNative();
    public static native void releaseSpecialMembers();
}
