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
    public static class OrdinaryOwner { public static int value() { return 3; } }
    public static class OrdinaryMiddle extends OrdinaryOwner { }
    public static class OrdinaryLeaf extends OrdinaryMiddle { }
    public static class OrdinaryHiding extends OrdinaryLeaf {
        public static int value() { return 5; }
    }
    static class PackageOwner { public static int value() { return 7; } }
    static class PackageLeaf extends PackageOwner { }
    public static void runNonpublicStatic() throws Throwable {
        for (Class<?> owner : new Class<?>[]{PackageOwner.class, PackageLeaf.class}) {
            int value = (int) MethodHandles.lookup().findStatic(
                    owner, "value", MethodType.methodType(int.class)).invokeExact();
            if (value != 7) throw new AssertionError("nonpublic class static lookup changed target");
            try {
                MethodHandles.publicLookup().findStatic(owner, "value", MethodType.methodType(int.class));
                throw new AssertionError("nonpublic class access check bypassed");
            } catch (IllegalAccessException expected) { }
        }
    }
    public static void runInheritedStatic() throws Throwable {
        int inherited = (int) MethodHandles.publicLookup().findStatic(
                OrdinaryLeaf.class, "value", MethodType.methodType(int.class)).invokeExact();
        int hidden = (int) MethodHandles.publicLookup().findStatic(
                OrdinaryHiding.class, "value", MethodType.methodType(int.class)).invokeExact();
        if (inherited != 3 || hidden != 5) throw new AssertionError("class static resolution changed target");
        try {
            MethodHandles.publicLookup().findStatic(OrdinaryLeaf.class, "value", MethodType.methodType(String.class));
            throw new AssertionError("inherited wrong descriptor accepted");
        } catch (NoSuchMethodException expected) { }
        try {
            OrdinaryLeaf.class.getDeclaredMethod("value");
            throw new AssertionError("ordinary declared reflection changed");
        } catch (NoSuchMethodException expected) { }
    }
    public static void runStatic() throws Throwable {
        try {
            MethodHandles.publicLookup().findStatic(Child.class, "inherited", MethodType.methodType(int.class));
            throw new AssertionError("inherited instance static lookup accepted");
        } catch (IllegalAccessException expected) { }
        try {
            MethodHandles.publicLookup().findStatic(Child.class, "inherited", MethodType.methodType(String.class));
            throw new AssertionError("wrong static return lookup accepted");
        } catch (NoSuchMethodException expected) { }
        // The guard must also observe the ordinary-class live MethodType;
        // matching identities still reach ART and invoke the actual body.
        int value = (int) MethodHandles.publicLookup().findStatic(
                OrdinaryOwner.class, "value", MethodType.methodType(int.class)).invokeExact();
        if (value != 3) throw new AssertionError("ordinary static lookup changed target");
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
