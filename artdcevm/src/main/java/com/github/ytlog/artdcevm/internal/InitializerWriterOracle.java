package com.github.ytlog.artdcevm.internal;
/** Fresh private source Class, held under its actual Java monitor by the native producer. */
public final class InitializerWriterOracle {
    private InitializerWriterOracle() {}
    private static native boolean observe(Class<?> type, int publicTid);
    private static int bodies;
    public static Class<?> type() { return Active.class; }
    public static int reenter() { return Active.marker; }
    public static int run() {
        int result = Active.marker;
        if (bodies != 1 || result != 0x13718419) throw new IllegalStateException("Writer body oracle");
        return result;
    }
    private static final class Active {
        static final int marker = begin();
        private static int begin() {
            bodies++;
            if (!observe(Active.class, android.os.Process.myTid())) {
                throw new IllegalStateException("Native initializer writer oracle rejected");
            }
            // The native round trip must restore the actual owner tid before
            // any public recursive source access can observe this class.
            if (InitializerWriterOracle.reenter() != 0) throw new IllegalStateException("Wrong recursive default");
            return 0x13718419;
        }
    }
}
