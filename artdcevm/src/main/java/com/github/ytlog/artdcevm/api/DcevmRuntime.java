package com.github.ytlog.artdcevm.api;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Debug;
import com.github.ytlog.artdcevm.internal.RetiredField;
import com.github.ytlog.artdcevm.internal.RetiredStaticField;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Entry point of the structural redefinition engine.
 *
 * <p>After {@link #initialize(Context)} succeeds, submit the caller agent's standard
 * JVMTI {@code RedefineClasses} directly. Return {@code 0} ({@code JVMTI_ERROR_NONE})
 * means success. Otherwise, handle the returned JVMTI error code and read
 * {@code lastRejection} from {@link #capabilities()}; rejected batches leave the
 * old definitions unchanged.
 * {@code capabilities()} is diagnostic only, never a pre-submission admission check.
 * Do not use its booleans or state strings to allow or block submission.
 *
 * <p>初始化成功后，直接提交调用方 Agent 的标准 {@code RedefineClasses}。
 * 返回 {@code 0} 表示成功；否则读取错误码及 {@code lastRejection}，旧定义保持不变。
 * {@code capabilities()} 只用于诊断，不作为提交前的准入判断。
 */
public final class DcevmRuntime {
    private static boolean attached;
    private static boolean initialized;

    private DcevmRuntime() {}

    /**
     * Loads the library, attaches it as a JVMTI agent and installs the engine.
     * Repeated calls after success return immediately.
     *
     * @throws SecurityException if the app was not debuggable from process start
     * @throws IllegalStateException if the engine cannot be installed on this runtime
     */
    public static synchronized void initialize(Context context) throws Exception {
        if (initialized) return;
        attach(context);
        // Class literals do not initialize RetiredStaticField.
        int code = nativeInitialize(RetiredField.class, RetiredStaticField.class);
        if (code != 0) {
            throw new IllegalStateException("ART DCEVM initialization failed: " + nativeErrorName(code)
                    + " (" + code + "); " + capabilities().optString("diagnostic"));
        }
        initialized = true;
    }

    /**
     * Returns installation/probe diagnostics and the latest standard-entry rejection.
     * Reading the report does not run probes. Its fields do not decide whether a
     * request may be submitted: call standard JVMTI {@code RedefineClasses} directly.
     * Return {@code 0} means success; otherwise read the error code and
     * {@code lastRejection}. The latter is cleared after a successful transaction.
     *
     * <p>This method is diagnostic only, never a pre-submission admission check.
     * Do not use report booleans or state strings to block submission. Supported
     * changes and errors are defined by the transaction's checks and the project's
     * limitations documentation, not by interpreting this snapshot.
     *
     * <p>本方法只用于诊断，不作为提交前的准入判断；读取本身不触发探测。
     * 直接提交 {@code RedefineClasses}，以 {@code 0} 判断成功；非零时读取错误码和
     * {@code lastRejection}，不要用报告中的布尔值或状态字符串拦截提交。
     *
     * @return the current diagnostic report, or an empty object before attachment
     */
    public static synchronized JSONObject capabilities() {
        if (!attached) return new JSONObject();
        try {
            return new JSONObject(nativeCapabilities());
        } catch (JSONException e) {
            throw new IllegalStateException("Malformed capability report", e);
        }
    }

    private static void attach(Context context) throws Exception {
        if (attached) return;
        Context application = context.getApplicationContext();
        if ((application.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0)
            throw new SecurityException("JVMTI requires an app that is debuggable from process start");
        System.loadLibrary("artdcevm");
        // The class loader resolves the library name, so install paths containing '=' work.
        Debug.attachJvmtiAgent("libartdcevm.so", null, application.getClassLoader());
        attached = true;
    }

    private static native int nativeInitialize(Class<?> retiredField, Class<?> retiredStaticField);
    private static native String nativeCapabilities();
    private static native String nativeErrorName(int code);
}
