package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/** Native-phase proof of public JVMTI access to libcore's live lookup argument. */
public final class MethodHandleLookupProbe {
    private MethodHandleLookupProbe() { }
    public interface StaticOwner { static int value() { return 1; } }
    public interface Parent { default int inherited() { return 2; } }
    public interface Child extends Parent { }
    public interface Leaf extends Child { }
    public static void runStatic() throws NoSuchMethodException, IllegalAccessException {
        try {
            MethodHandles.publicLookup().findStatic(Child.class, "inherited", MethodType.methodType(int.class));
            throw new AssertionError("inherited instance static lookup accepted");
        } catch (IllegalAccessException expected) { }
        try {
            MethodHandles.publicLookup().findStatic(Child.class, "inherited", MethodType.methodType(String.class));
            throw new AssertionError("wrong static return lookup accepted");
        } catch (NoSuchMethodException expected) { }
    }
    public static void run() throws NoSuchMethodException, IllegalAccessException {
        try {
            MethodHandles.publicLookup().findVirtual(StaticOwner.class, "value", MethodType.methodType(int.class));
            throw new AssertionError("static virtual lookup accepted");
        } catch (IllegalAccessException expected) { }
        // Requires the native guard to read findVirtual's MethodType through
        // the fixed recursive Class lookup frames before application admission.
        MethodHandles.publicLookup().findVirtual(Leaf.class, "inherited", MethodType.methodType(int.class));
    }
}
