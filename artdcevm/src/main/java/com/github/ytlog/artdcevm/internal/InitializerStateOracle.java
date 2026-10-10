package com.github.ytlog.artdcevm.internal;

/** Private one-shot public initialization oracle; no application class is mutated. */
public final class InitializerStateOracle {
    private InitializerStateOracle() {}
    private static native boolean observe(Class<?> type, int tid, int descriptorHash);
    public static Class<?> inactiveClass() { return Active.class; }
    public static int run() { return Active.marker; }
    private static final class Active {
        static final int marker;
        static {
            String descriptor = "L" + Active.class.getName().replace('.', '/') + ";";
            if (!observe(Active.class, android.os.Process.myTid(), descriptor.hashCode())) {
                throw new IllegalStateException("Native class initialization observation failed");
            }
            marker = 0x18491315;
        }
    }
}
