package com.github.ytlog.artdcevm.engine;

/** Stable application-to-agent protocol; contains no ART layouts or private symbols. */
public final class NativeBridge {
    private NativeBridge() {}
    public static native String capabilities();
    public static native int apply(int operation, Class<?>[] classes, byte[][] dex);
    public static native String analyze(Class<?>[] classes, byte[][] dex);
    public static native String errorName(int error);
    public static native int enableValidationHooks();
    public static native int initializeEvolution(String modulePath, Object calibration);
}
