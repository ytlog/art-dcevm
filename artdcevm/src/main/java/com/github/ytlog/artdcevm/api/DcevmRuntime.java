package com.github.ytlog.artdcevm.api;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Debug;
import org.json.JSONObject;

/**
 * 增强库的唯一 Java 初始化入口。
 *
 * <p>负责加载 SO、通过应用 ClassLoader 附加 JVMTI Agent，并发起完整演进初始化。
 * Android 版本选择、设备匹配、ART Hook 和结构演进均由 Native 层负责。
 * 替换请求和测试探针位于 debug 测试应用，不属于此 API。
 */
public final class DcevmRuntime {
    private static boolean attached;
    private static boolean evolutionInitialized;

    private DcevmRuntime() {}

    /**
     * 初始化完整演进能力；成功后，同一进程内的重复调用直接返回。
     *
     * <p>应用必须从进程启动起可调试。Native 层将检查设备与校准数据，
     * 未适配或校准失败时抛出异常，不自动退化为较弱的替换操作。
     *
     * @param context 用于取得应用 Context 和 ClassLoader
     * @throws Exception Agent 附加失败或完整演进初始化失败
     */
    public static synchronized void initialize(Context context) throws Exception {
        if (evolutionInitialized) return;
        attach(context);
        int code = nativeInitializeEvolution();
        if (code != 0)
            throw new IllegalStateException("ART redefine initialization failed: " + code
                    + ": " + nativeErrorName(code) + "; "
                    + new JSONObject(nativeCapabilities()).optString("evolutionError"));
        evolutionInitialized = true;
    }

    /**
     * 仅附加公开 JVMTI，返回当前后端能力，供不需要完整演进的调用方使用。
     *
     * <p>私有 ART 路由不可用时，仍保留公开方法体替换能力；
     * 此方法不会将完整演进标记为初始化成功。
     */
    public static synchronized JSONObject attachPublicJvmti(Context context) throws Exception {
        attach(context);
        // 私有路由不可用，不应阻止公开 JVMTI 方法体替换。
        nativeEnableStandardRedefineRouting();
        return new JSONObject(nativeCapabilities());
    }

    /** 加载与附加统一串行化；仅在 Agent 附加成功后记录状态，失败可重试。 */
    private static void attach(Context context) throws Exception {
        if (attached) return;
        Context application = context.getApplicationContext();
        if ((application.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0)
            throw new SecurityException("JVMTI requires a debuggable app from process start");
        System.loadLibrary("artdcevm");
        // 安装路径可能含 '='，由 ClassLoader 解析库名，避免传入不被接受的绝对路径。
        Debug.attachJvmtiAgent("libartdcevm.so", null, application.getClassLoader());
        attached = true;
    }

    // 仅保留初始化与错误诊断所需的 JNI；替换/分析等测试 JNI 只在 debug 构建导出。
    private static native String nativeCapabilities();
    private static native String nativeErrorName(int code);
    private static native int nativeEnableStandardRedefineRouting();
    private static native int nativeInitializeEvolution();

}
