package com.github.ytlog.artdcevm.testing;

/**
 * 库自己管理的 debug 专用 JNI 测试桥，供测试应用调用。
 *
 * <p>Android Gradle Plugin 仅将此源码包含在 Debug 变体；对应 Native 入口
 * 由 ARTDCEVM_TESTING 控制。Release AAR 与 SO 均不包含这些测试接口。
 * 这里的 public 仅用于跨模块测试访问，不属于发布 SDK 的公共 API。
 */
public final class NativeBridge {
    private NativeBridge() {}
    public static native String capabilities();
    public static native int apply(int operation, Class<?>[] classes, byte[][] dex);
    public static native String analyze(Class<?>[] classes, byte[][] dex);
    public static native String errorName(int error);
    public static native int enableValidationHooks();
    public static native int enableStandardRedefineRouting();
    public static native int initializeEvolution();
    /** Selects the entire shared owner before any legacy private hook installation. */
    public static native int initializeCommonCapabilities(Object calibration);
}
