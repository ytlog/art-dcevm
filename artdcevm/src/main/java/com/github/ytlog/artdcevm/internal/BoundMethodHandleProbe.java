package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/** Native preparation proves that ART reports the real BindTo entry. */
public final class BoundMethodHandleProbe {
    private BoundMethodHandleProbe() {}
    public interface Contract { Object value(); }
    private static final class Target implements Contract {
        public Object value() { return this; }
    }
    public static void run() throws Throwable {
        Object value = new Target();
        MethodHandle bound = MethodHandles.lookup().findVirtual(
                Contract.class, "value", MethodType.methodType(Object.class)).bindTo(value);
        if (bound.invokeWithArguments() != value) throw new AssertionError("bound entry probe");
    }
}
