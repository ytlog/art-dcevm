package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

/** Native preparation proves that ART reports the real BindTo entry. */
public final class BoundMethodHandleProbe {
    private BoundMethodHandleProbe() {}
    public interface Contract { Object value(Object marker, int count, long bits, double fraction); }
    private static final class Target implements Contract {
        int calls;
        public Object value(Object marker, int count, long bits, double fraction) {
            if (marker != this || count != 7 || bits != 0x123456789abcdefL || fraction != 0.5)
                throw new AssertionError("captured argument copy");
            calls++;
            return this;
        }
    }
    public static void run() throws Throwable {
        Target value = new Target();
        MethodHandle bound = MethodHandles.lookup().findVirtual(
                Contract.class, "value", MethodType.methodType(Object.class, Object.class, int.class, long.class, double.class)).bindTo(value);
        if (bound.invokeWithArguments(value, 7, 0x123456789abcdefL, 0.5) != value || value.calls != 1) throw new AssertionError("bound entry probe");
    }
}
