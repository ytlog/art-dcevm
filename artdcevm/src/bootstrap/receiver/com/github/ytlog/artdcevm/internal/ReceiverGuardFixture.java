package com.github.ytlog.artdcevm.internal;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.invoke.VarHandle;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** Public original-entry and readback witnesses. No rejection is simulated here. */
public final class ReceiverGuardFixture {
    public static class Base {
        public int value = 71;
        public int body() { bodies++; return 113; }
    }
    public static final class Receiver extends Base {
        public int callSuper() { return super.body(); }
        static MethodHandle originalSpecial() throws ReflectiveOperationException {
            return MethodHandles.lookup().findSpecial(Base.class, "body",
                    MethodType.methodType(int.class), Receiver.class);
        }
    }
    private static long bodies;
    private static long sentinel = 0x1020304050607080L;
    // Both references and members are retained from before the source migration.
    public final Receiver receiver = new Receiver();
    public final Method method = Base.class.getDeclaredMethod("body");
    public final Field field = Base.class.getDeclaredField("value");
    public final MethodHandle invoke = MethodHandles.lookup().unreflect(method);
    public final MethodHandle read = MethodHandles.lookup().unreflectGetter(field);
    public final MethodHandle write = MethodHandles.lookup().unreflectSetter(field);
    public final MethodHandle special = Receiver.originalSpecial();
    public final VarHandle variable = MethodHandles.lookup().findVarHandle(Base.class, "value", int.class);
    public int writeValue = 89;
    public ReceiverGuardFixture() throws ReflectiveOperationException {}

    // JNI registration is supplied by the unique native production entry owner.
    // An absent implementation raises ULE and cannot count as a proof episode.
    private static native int jniMethod(Base receiver);
    private static native int jniRead(Base receiver);
    private static native void jniWrite(Base receiver, int value);
    private static native int nativeInvoke(Base receiver);

    public int execute(int path) throws Throwable {
        switch (path) {
            case 0: return receiver.body();
            case 1: return receiver.callSuper();
            case 2: return receiver.value;
            case 3: receiver.value = writeValue; return writeValue;
            case 4: return jniMethod(receiver);
            case 5: return jniRead(receiver);
            case 6: jniWrite(receiver, writeValue); return writeValue;
            case 7:
                try { return ((Integer) method.invoke(receiver)).intValue(); }
                catch (InvocationTargetException wrapped) { throw wrapped.getCause(); }
            case 8: return field.getInt(receiver);
            case 9: field.setInt(receiver, writeValue); return writeValue;
            case 10: return (int) invoke.invokeExact((Base) receiver);
            case 11: return (int) read.invokeExact((Base) receiver);
            case 12: write.invokeExact((Base) receiver, writeValue); return writeValue;
            case 13: return (int) special.invokeExact(receiver);
            case 14: return (int) variable.get(receiver);
            case 15: return nativeInvoke(receiver);
            default: throw new IllegalArgumentException("Unknown receiver path " + path);
        }
    }
    // Native readback must use independently calibrated retained-cell access
    // after an old receiver loses Base. Calling the cached Base field here
    // would itself reject, so this only supplies the independent body/sentinel.
    public static long[] counters() { return new long[] {bodies, sentinel}; }
}
