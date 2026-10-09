package com.github.ytlog.artdcevm.testing;

import android.os.Process;
import java.lang.reflect.Method;

/** Real ARM32 JNI receiver proof; does not initialize full private evolution. */
public final class Arm32ReceiverProbe {
    private Arm32ReceiverProbe() {}
    public static String metadata(Class<?> owner, java.lang.reflect.Executable[] methods, MethodDexOracle.Result oracle) {
        return metadata(owner, methods, oracle, false);
    }
    public static String metadata(Class<?> owner, java.lang.reflect.Executable[] methods, MethodDexOracle.Result oracle, boolean originalFieldsOnly) {
        if (Process.is64Bit()) return "{\"passed\":false,\"diagnostic\":\"ARM32 metadata process required\"}";
        System.loadLibrary("artdcevm");
        return nativeMetadata(owner, methods, oracle.indices, oracle.modifiers, oracle.directMethods, oracle.virtualMethods, oracle.classDefIndex, oracle.typeIndex, owner.getModifiers(), oracle.fields, oracle.fieldIndices, oracle.fieldModifiers, oracle.staticFields, originalFieldsOnly);
    }
    private static native String nativeMetadata(Class<?> owner, java.lang.reflect.Executable[] methods, int[] indices, int[] flags, int direct, int virtualCount, int classDef, int typeIndex, int classFlags, java.lang.reflect.Field[] fields, int[] fieldIndices, int[] fieldFlags, int staticFields, boolean originalFieldsOnly);
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
