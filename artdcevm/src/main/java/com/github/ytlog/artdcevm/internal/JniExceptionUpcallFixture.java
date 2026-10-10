package com.github.ytlog.artdcevm.internal;

/** Owned ordinary JNI propagation witness; never grants boot-frame permission. */
public final class JniExceptionUpcallFixture {
    private JniExceptionUpcallFixture() {}

    public static void throwSame(Throwable expected) throws Throwable {
        throw expected;
    }

    private static native long prepare(Throwable expected);
    private static native void invoke(long request);
    private static native boolean complete(long request, Throwable caught);
    private static native void cancel(long request);

    public static boolean run(Throwable expected) {
        if (expected == null) return false;
        long request = prepare(expected);
        if (request == 0) return false;
        try {
            invoke(request);
        } catch (Throwable caught) {
            // This is the actual Java catch result, not a reconstructed token.
            return complete(request, caught);
        } finally {
            cancel(request); // Also handles an interrupted or incomplete witness.
        }
        return false;
    }
}
