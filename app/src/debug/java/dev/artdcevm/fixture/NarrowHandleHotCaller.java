package dev.artdcevm.fixture;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.invoke.VarHandle;

/** Cached signature-polymorphic call sites for superclass evolution probes. */
@android.annotation.TargetApi(33)
public final class NarrowHandleHotCaller {
    private static final MethodHandle GETTER;
    private static final MethodHandle METHOD;
    private static final VarHandle FIELD;

    static {
        try {
            MethodHandles.Lookup lookup = MethodHandles.lookup();
            GETTER = lookup.findGetter(NarrowParent.class, "ancestor", int.class);
            METHOD = lookup.findVirtual(NarrowParent.class, "inherited", MethodType.methodType(int.class));
            FIELD = lookup.findVarHandle(NarrowParent.class, "ancestor", int.class);
        } catch (ReflectiveOperationException error) {
            throw new ExceptionInInitializerError(error);
        }
    }

    private NarrowHandleHotCaller() {}

    public static int readExact(NarrowParent receiver) throws Throwable {
        return (int) GETTER.invokeExact(receiver);
    }

    public static int invokeExact(NarrowParent receiver) throws Throwable {
        return (int) METHOD.invokeExact(receiver);
    }

    public static int readVar(NarrowParent receiver) {
        return (int) FIELD.get(receiver);
    }

    public static void writeVar(NarrowParent receiver, int value) {
        FIELD.set(receiver, value);
    }

    public static boolean compareVar(NarrowParent receiver, int expected, int value) {
        return FIELD.compareAndSet(receiver, expected, value);
    }

    public static int readOpaque(NarrowParent receiver) {
        return (int) FIELD.getOpaque(receiver);
    }

    public static int readVolatile(NarrowParent receiver) {
        return (int) FIELD.getVolatile(receiver);
    }

    public static void writeVolatile(NarrowParent receiver, int value) {
        FIELD.setVolatile(receiver, value);
    }

    public static int getAndSet(NarrowParent receiver, int value) {
        return (int) FIELD.getAndSet(receiver, value);
    }

    public static int compareAndExchange(NarrowParent receiver, int expected, int value) {
        return (int) FIELD.compareAndExchange(receiver, expected, value);
    }

    public static int getAndAdd(NarrowParent receiver, int delta) {
        return (int) FIELD.getAndAdd(receiver, delta);
    }
}
