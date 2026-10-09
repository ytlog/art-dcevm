package com.github.ytlog.artdcevm.internal;

/** Actual public reflection and original-call evidence for external vtables. */
public final class AbstractVTableOracle {
    private AbstractVTableOracle() {}
    public abstract static class Incomplete { public abstract int value(); }
    public abstract static class Complete extends Incomplete {
        @Override public int value() { return 41; }
    }
    public static final class Concrete extends Complete {
        @Override public int value() { return 41; }
    }
    public static class VirtualBase { public int dispatch() { return 31; } }
    public static final class VirtualChild extends VirtualBase {
        @Override public int dispatch() { return 43; }
    }
    private static class PrivateBase { private int direct() { return 17; } }
    private static final class PrivateChild extends PrivateBase {
        public int direct() { return 29; }
    }
    private static void checkOriginalLookups() throws ReflectiveOperationException {
        java.lang.invoke.MethodHandles.Lookup lookup = java.lang.invoke.MethodHandles.lookup();
        java.lang.reflect.Method reflected = PrivateBase.class.getDeclaredMethod("direct");
        reflected.setAccessible(true);
        java.lang.invoke.MethodHandle direct = lookup.unreflect(reflected);
        java.lang.invoke.MethodHandle virtual = lookup.findVirtual(VirtualBase.class, "dispatch",
            java.lang.invoke.MethodType.methodType(int.class));
        try {
            // Original lookup mode remains distinct from a subsequent fresh
            // virtual lookup: private calls do not dispatch to a same-name child.
            if ((int)direct.invoke(new PrivateChild()) != 17 ||
                (int)virtual.invoke(new VirtualChild()) != 43)
                throw new IllegalStateException("Original lookup dispatch differs");
        } catch (RuntimeException | Error failure) { throw failure;
        } catch (Throwable failure) { throw new IllegalStateException("Original method handle failed", failure); }
    }
    public static Object[] create() throws ReflectiveOperationException {
        checkOriginalLookups();
        if (new Concrete().value() != 41) throw new IllegalStateException("Original complete call differs");
        return new Object[] {Incomplete.class, Complete.class, Concrete.class,
            Incomplete.class.getDeclaredMethod("value"), Complete.class.getDeclaredMethod("value"),
            Concrete.class.getDeclaredMethod("value")};
    }
}
