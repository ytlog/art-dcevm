package com.github.ytlog.artdcevm.internal;

/** Private one-shot public initialization oracle; no application class is mutated. */
public final class InitializerStateOracle {
    private InitializerStateOracle() {}
    private static native boolean observe(Class<?> type, int tid, int descriptorHash);
    public static Class<?> inactiveClass() { return Active.class; }
    public static int run() { return Active.marker; }
    // Called by the native observer while this very thread owns Active initialization.
    // This must enter the original ClassLinker recursive path and see the default cell.
    public static int reenter() { return Active.readCurrent(); }
    private static int failureBodies;
    public static Class<?> failedClass() { return Failure.class; }
    public static int runFailure() {
        try {
            int ignored = Failure.marker;
            throw new IllegalStateException("Expected first initialization failure: " + ignored);
        } catch (ExceptionInInitializerError first) {
            if (!(first.getCause() instanceof IllegalArgumentException)) throw first;
        }
        try {
            int ignored = Failure.marker;
            throw new IllegalStateException("Expected poisoned class: " + ignored);
        } catch (NoClassDefFoundError expected) {
            if (failureBodies != 1) throw new IllegalStateException("Initializer was rerun");
            return failureBodies;
        }
    }
    private static final class Failure {
        static final int marker = fail();
        private static int fail() {
            failureBodies++;
            throw new IllegalArgumentException("Private initialization poison oracle");
        }
    }
    private static final class Active {
        static final int marker;
        static int readCurrent() { return marker; }
        static {
            String descriptor = "L" + Active.class.getName().replace('.', '/') + ";";
            if (!observe(Active.class, android.os.Process.myTid(), descriptor.hashCode())) {
                throw new IllegalStateException("Native class initialization observation failed");
            }
            marker = 0x18491315;
        }
    }
}
