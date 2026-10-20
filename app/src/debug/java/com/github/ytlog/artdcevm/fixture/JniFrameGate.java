package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Shared JVM/ART gate; every parked invocation enters exactly once. */
public final class JniFrameGate {
    public interface Call { Object run() throws Throwable; }
    private static final ThreadLocal<Parked> CURRENT = new ThreadLocal<>();
    private JniFrameGate() {}

    public static Object observe(Call call) {
        try { return call.run(); }
        catch (Throwable error) { return error.getClass().getName(); }
    }
    public static int pause(int value) {
        Parked parked = CURRENT.get();
        if (parked != null && !parked.used) {
            parked.used = true;
            parked.held = true;
            parked.entered.countDown();
            try {
                if (!parked.resume.await(90, TimeUnit.SECONDS)) throw new AssertionError("Frame release timed out");
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new AssertionError(error);
            }
        }
        return value;
    }
    public static int checked(Object receiver, int value) {
        try {
            // Also discover a constructor from an old target frame: history
            // capture must never encode application stack frames into JNI IDs.
            if (receiver.getClass().getDeclaredConstructor().getDeclaringClass() != receiver.getClass())
                throw new AssertionError("Constructor lookup changed declaring identity");
            if (receiver.getClass().getField("self").get(receiver) != receiver)
                throw new AssertionError("Old Java frame lost self identity");
            int kept = receiver.getClass().getField("kept").getInt(receiver);
            int expected = receiver.getClass().getName().endsWith("$Own") ? 23 : 19;
            if (kept != expected) throw new AssertionError("Old Java frame lost a retained field");
        } catch (ReflectiveOperationException error) { throw new AssertionError(error); }
        return value;
    }
    public static final class Parked implements AutoCloseable {
        private final CountDownLatch entered = new CountDownLatch(1), resume = new CountDownLatch(1);
        private final Thread worker;
        private boolean used;
        private volatile boolean held;
        private volatile Object result;
        public Parked(Call call, int index) {
            worker = new Thread(() -> {
                CURRENT.set(this);
                try { result = observe(call); }
                finally { entered.countDown(); CURRENT.remove(); }
            }, "jni-frame-worker-" + index);
            worker.start();
        }
        public void awaitHeld() throws InterruptedException {
            if (!entered.await(30, TimeUnit.SECONDS)) throw new AssertionError("Frame entry timed out");
            if (!held) throw new AssertionError("Invocation completed before parking: " + result);
        }
        public Object finish() throws InterruptedException {
            resume.countDown();
            worker.join(30000);
            if (worker.isAlive()) throw new AssertionError("Frame did not return");
            return result;
        }
        public void release() { resume.countDown(); }
        @Override public void close() throws InterruptedException { finish(); }
    }
}
