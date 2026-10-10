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
        private volatile SuperCoordinator superCoordinator;
        public int callSuper() throws InterruptedException {
            SuperCoordinator coordinator = superCoordinator;
            if (coordinator == null) return super.body();
            try {
                coordinator.awaitBeforeSuper(this);
                return super.body();
            } finally {
                coordinator.complete(this);
            }
        }
        public synchronized SuperCoordinator coordinateSuper(long timeoutMillis) {
            if (superCoordinator != null) throw new IllegalStateException("Super coordination already armed");
            superCoordinator = new SuperCoordinator(this, timeoutMillis);
            return superCoordinator;
        }
        static MethodHandle originalSpecial() throws ReflectiveOperationException {
            return MethodHandles.lookup().findSpecial(Base.class, "body",
                    MethodType.methodType(int.class), Receiver.class);
        }
    }
    /** An ordinary rooted object; waiting releases its monitor. This is only
     * a public execution-order oracle, never a migration/frame permission. */
    public static final class SuperCoordinator {
        private final Receiver receiver;
        private final long timeoutNanos;
        private Thread enteredThread;
        private boolean entered, released, cancelled, completed;
        private SuperCoordinator(Receiver receiver, long timeoutMillis) {
            if (timeoutMillis <= 0 || timeoutMillis > Long.MAX_VALUE / 1000000L)
                throw new IllegalArgumentException("Positive bounded timeout required");
            this.receiver = receiver;
            this.timeoutNanos = timeoutMillis * 1000000L;
        }
        private synchronized void awaitBeforeSuper(Receiver actual) throws InterruptedException {
            if (actual != receiver || entered) throw new IllegalStateException("Wrong receiver or repeated entry");
            entered = true;
            enteredThread = Thread.currentThread();
            notifyAll();
            long start = System.nanoTime();
            try {
                while (!released && !cancelled) {
                    long remaining = timeoutNanos - (System.nanoTime() - start);
                    if (remaining <= 0) { cancelled = true; break; }
                    wait(remaining / 1000000L, (int) (remaining % 1000000L));
                }
            } catch (InterruptedException interrupted) {
                cancelled = true;
                notifyAll();
                Thread.currentThread().interrupt();
                throw interrupted;
            }
            if (cancelled) throw new InterruptedException("Super coordination cancelled or timed out");
        }
        private synchronized void complete(Receiver actual) {
            if (actual != receiver || enteredThread != Thread.currentThread())
                throw new IllegalStateException("Wrong super completion owner");
            completed = true;
            notifyAll();
        }
        public synchronized boolean awaitEntered(long timeoutMillis) throws InterruptedException {
            if (timeoutMillis <= 0 || timeoutMillis > Long.MAX_VALUE / 1000000L)
                throw new IllegalArgumentException("Positive bounded timeout required");
            long duration = timeoutMillis * 1000000L, start = System.nanoTime();
            try {
                while (!entered && !cancelled) {
                    long remaining = duration - (System.nanoTime() - start);
                    if (remaining <= 0) return false;
                    wait(remaining / 1000000L, (int) (remaining % 1000000L));
                }
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw interrupted;
            }
            return entered;
        }
        public synchronized boolean release() {
            if (!entered || completed || released || cancelled) return false;
            released = true;
            notifyAll();
            return true;
        }
        public synchronized boolean cancel() {
            if (completed || released || cancelled) return false;
            cancelled = true;
            notifyAll();
            return true;
        }
        public Receiver receiver() { return receiver; }
        public synchronized Thread enteredThread() { return enteredThread; }
        public synchronized boolean entered() { return entered; }
        public synchronized boolean released() { return released; }
        public synchronized boolean cancelled() { return cancelled; }
        public synchronized boolean completed() { return completed; }
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
