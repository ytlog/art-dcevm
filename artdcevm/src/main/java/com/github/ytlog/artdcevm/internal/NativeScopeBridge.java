package com.github.ytlog.artdcevm.internal;

/** Ordinary JNI carrier. ART owns the JNI frame, cookie and Native transitions. */
public final class NativeScopeBridge {
    private NativeScopeBridge() {}
    // Deliberately ordinary JNI: neither FastNative nor CriticalNative.
    private static native void enter(long nonce);
}
