package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/** Native-phase proof of public JVMTI access to libcore's live lookup argument. */
public final class MethodHandleLookupProbe {
    private MethodHandleLookupProbe() { }
    public interface StaticOwner { static int value() { return 1; } }
    public static void run() throws NoSuchMethodException {
        try {
            MethodHandles.publicLookup().findVirtual(StaticOwner.class, "value", MethodType.methodType(int.class));
            throw new AssertionError("static virtual lookup accepted");
        } catch (IllegalAccessException expected) { }
    }
}
