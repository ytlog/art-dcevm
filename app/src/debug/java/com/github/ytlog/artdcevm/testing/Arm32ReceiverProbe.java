package com.github.ytlog.artdcevm.testing;

import android.os.Process;
import java.lang.reflect.Method;

/** Real ARM32 JNI receiver proof; does not initialize full private evolution. */
public final class Arm32ReceiverProbe {
    private Arm32ReceiverProbe() {}
    private static native String nativeCalibrate(Method immutableBootMethod);

    public static String run() throws ReflectiveOperationException {
        return run(Object.class.getDeclaredMethod("toString"));
    }

    public static String run(Method carrier) {
        if (Process.is64Bit()) {
            return "{\"passed\":false,\"diagnostic\":\"ARM32 process required\"}";
        }
        System.loadLibrary("artdcevm");
        return nativeCalibrate(carrier);
    }
}
