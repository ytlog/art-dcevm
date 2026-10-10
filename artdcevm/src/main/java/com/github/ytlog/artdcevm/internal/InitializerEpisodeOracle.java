package com.github.ytlog.artdcevm.internal;

/** Each private class executes its original ART initialization exactly once. */
public final class InitializerEpisodeOracle {
    private InitializerEpisodeOracle() {}
    private static native boolean body(Class<?> type, int tid);
    private static native boolean waiter();
    private static volatile Throwable ownerFailure, waiterFailure;
    private static volatile int ownerResult, waiterResult, bodies;
    public static Class<?> type(boolean fail) { return fail ? Failure.class : Success.class; }
    public static int run(boolean fail) {
        ownerFailure = null; waiterFailure = null; ownerResult = 0; waiterResult = 0; bodies = 0;
        Thread owner = new Thread(() -> {
            try { ownerResult = fail ? Failure.marker : Success.marker; }
            catch (Throwable t) { ownerFailure = t; }
        }, "artdcevm-init-owner");
        Thread waiting = new Thread(() -> {
            try {
                if (!waiter()) throw new IllegalStateException("Waiter owner observation failed");
                waiterResult = fail ? Failure.marker : Success.marker;
            } catch (Throwable t) { waiterFailure = t; }
        }, "artdcevm-init-waiter");
        boolean ownerStarted = false, waiterStarted = false;
        try {
            owner.start(); ownerStarted = true;
            waiting.start(); waiterStarted = true;
        } finally {
            // Never return a native Trial while a hook/body can still borrow it,
            // including thread-start failure or an interrupted calibrating caller.
            boolean interrupted = ownerStarted && join(owner);
            interrupted |= waiterStarted && join(waiting);
            if (interrupted) Thread.currentThread().interrupt();
        }
        if (bodies != 1) throw new IllegalStateException("Initializer body count disagrees");
        if (fail) {
            if (!(ownerFailure instanceof ExceptionInInitializerError)
                    || !(ownerFailure.getCause() instanceof IllegalArgumentException)
                    || !(waiterFailure instanceof NoClassDefFoundError)) return 0;
            // The poisoned Class must never rerun the original body.
            try { int ignored = Failure.marker; return ignored; }
            catch (NoClassDefFoundError expected) { return bodies == 1 ? 1 : 0; }
        }
        return ownerFailure == null && waiterFailure == null
                && ownerResult == 0x31918413 && waiterResult == ownerResult ? 1 : 0;
    }
    private static boolean join(Thread thread) {
        boolean interrupted = false;
        for (;;) {
            try { thread.join(); return interrupted; }
            catch (InterruptedException expected) { interrupted = true; }
        }
    }
    private static int begin(Class<?> type) {
        bodies++;
        if (!body(type, android.os.Process.myTid()))
            throw new IllegalStateException("Original initialization/wait observation failed");
        return 0x31918413;
    }
    private static final class Success { static final int marker = begin(Success.class); }
    private static final class Failure {
        static final int marker = fail();
        private static int fail() {
            begin(Failure.class);
            throw new IllegalArgumentException("Private original initializer failure");
        }
    }
}
