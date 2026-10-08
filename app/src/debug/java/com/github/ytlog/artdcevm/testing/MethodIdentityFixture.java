package com.github.ytlog.artdcevm.testing;

/** Never redefine this class while its owned ArtMethod identity probe is running. */
public final class MethodIdentityFixture {
    public int instanceMarker(int value) { return value + 41; }
    public static final long staticMarker(long value) { return value ^ 0x123456789L; }
    private native void nativeMarker();
    public static int hotLoop(int count) {
        int value=17;
        for(int i=0;i<count;i++)value=Integer.rotateLeft(value^i,3)+0x3211;
        return value;
    }
}
