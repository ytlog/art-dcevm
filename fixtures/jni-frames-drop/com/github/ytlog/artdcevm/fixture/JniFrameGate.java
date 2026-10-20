package com.github.ytlog.artdcevm.fixture;

/** Compile-only signatures. The suite keeps the APK's real gate loaded. */
public final class JniFrameGate {
    public static native int pause(int value);
    public static native int checked(Object receiver, int value);
}
