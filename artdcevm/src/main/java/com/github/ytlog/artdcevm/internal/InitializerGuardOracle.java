package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/** Private one-shot constructor dispatch oracle; its constructor is retired by calibration. */
final class InitializerGuardOracle {
    public static int constructorCalls;
    public int intValue;

    public InitializerGuardOracle() {
        intValue = 0x81234567;
        constructorCalls++;
    }
    public static Object constructorHandle() throws ReflectiveOperationException {
        return MethodHandles.lookup().findConstructor(
                InitializerGuardOracle.class, MethodType.methodType(void.class));
    }

    public static Object invokeConstructorHandle(Object handle, boolean exact) throws Throwable {
        if (exact) return (InitializerGuardOracle) ((MethodHandle) handle).invokeExact();
        return ((MethodHandle) handle).invoke();
    }
}
