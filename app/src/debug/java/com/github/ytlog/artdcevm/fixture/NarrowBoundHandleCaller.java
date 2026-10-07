package com.github.ytlog.artdcevm.fixture;

import java.lang.invoke.MethodHandle;

public final class NarrowBoundHandleCaller {
    private NarrowBoundHandleCaller() {}

    public static int callExact(MethodHandle handle) throws Throwable {
        return (int) handle.invokeExact();
    }

    public static int callAdapted(MethodHandle handle) throws Throwable {
        return (int) handle.invokeWithArguments();
    }

    public static int callExactChecked(MethodHandle handle) {
        try { return callExact(handle); }
        catch (Throwable error) { throw new AssertionError(error); }
    }

    public static int callAdaptedChecked(MethodHandle handle) {
        try { return callAdapted(handle); }
        catch (Throwable error) { throw new AssertionError(error); }
    }
}
