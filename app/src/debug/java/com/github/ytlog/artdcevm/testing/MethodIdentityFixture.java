package com.github.ytlog.artdcevm.testing;

/** Never redefine this class while its owned ArtMethod identity probe is running. */
public final class MethodIdentityFixture {
    public int instanceMarker(int value) { return value + 41; }
    public static final long staticMarker(long value) { return value ^ 0x123456789L; }
    private native void nativeMarker();
}
