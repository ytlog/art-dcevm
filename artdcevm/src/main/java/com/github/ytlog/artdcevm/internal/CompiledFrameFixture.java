package com.github.ytlog.artdcevm.internal;

import java.lang.reflect.Method;

/** Library-owned physical caller/value oracle. Warmup never proves its ABI. */
public final class CompiledFrameFixture {
    public static volatile Object retained0, retained1, retained2;
    public static volatile int retainedInt0, retainedInt1, retainedInt2;
    private static volatile int compilerFence;
    public static native void awaitGate();
    private static native boolean armGate();
    private static native boolean waitEntered(int timeoutMillis);
    private static native void releaseGate();
    public int marker() { return 7; }
    private CompiledFrameFixture() {}
    public static int carrier(int a, Object ar, int b, Object br, int c, Object cr) {
        compilerFence ^= 1;
        compilerFence ^= 2;
        compilerFence ^= 3;
        compilerFence ^= 4;
        compilerFence ^= 5;
        compilerFence ^= 6;
        compilerFence ^= 7;
        compilerFence ^= 8;
        compilerFence ^= 9;
        compilerFence ^= 10;
        compilerFence ^= 11;
        compilerFence ^= 12;
        compilerFence ^= 13;
        compilerFence ^= 14;
        compilerFence ^= 15;
        compilerFence ^= 16;
        compilerFence ^= 17;
        compilerFence ^= 18;
        compilerFence ^= 19;
        compilerFence ^= 20;
        compilerFence ^= 21;
        compilerFence ^= 22;
        compilerFence ^= 23;
        compilerFence ^= 24;
        compilerFence ^= 25;
        compilerFence ^= 26;
        compilerFence ^= 27;
        compilerFence ^= 28;
        compilerFence ^= 29;
        compilerFence ^= 30;
        compilerFence ^= 31;
        compilerFence ^= 32;
        compilerFence ^= 33;
        compilerFence ^= 34;
        compilerFence ^= 35;
        compilerFence ^= 36;
        compilerFence ^= 37;
        compilerFence ^= 38;
        compilerFence ^= 39;
        compilerFence ^= 40;
        compilerFence ^= 41;
        compilerFence ^= 42;
        compilerFence ^= 43;
        compilerFence ^= 44;
        compilerFence ^= 45;
        compilerFence ^= 46;
        compilerFence ^= 47;
        compilerFence ^= 48;
        int nested = middle(a, ar, b, br, c, cr);
        retained0 = ar; retainedInt0 = a;
        return nested + a + System.identityHashCode(ar);
    }
    public static int middle(int a, Object ar, int b, Object br, int c, Object cr) {
        compilerFence ^= 1;
        compilerFence ^= 2;
        compilerFence ^= 3;
        compilerFence ^= 4;
        compilerFence ^= 5;
        compilerFence ^= 6;
        compilerFence ^= 7;
        compilerFence ^= 8;
        compilerFence ^= 9;
        compilerFence ^= 10;
        compilerFence ^= 11;
        compilerFence ^= 12;
        compilerFence ^= 13;
        compilerFence ^= 14;
        compilerFence ^= 15;
        compilerFence ^= 16;
        compilerFence ^= 17;
        compilerFence ^= 18;
        compilerFence ^= 19;
        compilerFence ^= 20;
        compilerFence ^= 21;
        compilerFence ^= 22;
        compilerFence ^= 23;
        compilerFence ^= 24;
        compilerFence ^= 25;
        compilerFence ^= 26;
        compilerFence ^= 27;
        compilerFence ^= 28;
        compilerFence ^= 29;
        compilerFence ^= 30;
        compilerFence ^= 31;
        compilerFence ^= 32;
        compilerFence ^= 33;
        compilerFence ^= 34;
        compilerFence ^= 35;
        compilerFence ^= 36;
        compilerFence ^= 37;
        compilerFence ^= 38;
        compilerFence ^= 39;
        compilerFence ^= 40;
        compilerFence ^= 41;
        compilerFence ^= 42;
        compilerFence ^= 43;
        compilerFence ^= 44;
        compilerFence ^= 45;
        compilerFence ^= 46;
        compilerFence ^= 47;
        compilerFence ^= 48;
        int nested = inner(a, ar, b, br, c, cr);
        retained1 = br; retainedInt1 = b;
        return nested + b + System.identityHashCode(br);
    }
    public static int inner(int a, Object ar, int b, Object br, int c, Object cr) {
        compilerFence ^= 1;
        compilerFence ^= 2;
        compilerFence ^= 3;
        compilerFence ^= 4;
        compilerFence ^= 5;
        compilerFence ^= 6;
        compilerFence ^= 7;
        compilerFence ^= 8;
        compilerFence ^= 9;
        compilerFence ^= 10;
        compilerFence ^= 11;
        compilerFence ^= 12;
        compilerFence ^= 13;
        compilerFence ^= 14;
        compilerFence ^= 15;
        compilerFence ^= 16;
        compilerFence ^= 17;
        compilerFence ^= 18;
        compilerFence ^= 19;
        compilerFence ^= 20;
        compilerFence ^= 21;
        compilerFence ^= 22;
        compilerFence ^= 23;
        compilerFence ^= 24;
        compilerFence ^= 25;
        compilerFence ^= 26;
        compilerFence ^= 27;
        compilerFence ^= 28;
        compilerFence ^= 29;
        compilerFence ^= 30;
        compilerFence ^= 31;
        compilerFence ^= 32;
        compilerFence ^= 33;
        compilerFence ^= 34;
        compilerFence ^= 35;
        compilerFence ^= 36;
        compilerFence ^= 37;
        compilerFence ^= 38;
        compilerFence ^= 39;
        compilerFence ^= 40;
        compilerFence ^= 41;
        compilerFence ^= 42;
        compilerFence ^= 43;
        compilerFence ^= 44;
        compilerFence ^= 45;
        compilerFence ^= 46;
        compilerFence ^= 47;
        compilerFence ^= 48;
        awaitGate();
        int nested = 0;
        retained2 = cr; retainedInt2 = c;
        return nested + c + System.identityHashCode(cr);
    }
    public static final class Run implements AutoCloseable {
        // Native factory obtains DEX indices, CodeItems and child invoke PCs
        // independently from the original installed APK byte[][], not ART.
        public final Method[] methods;
        public final Object[] roots = {new Object(), new Object(), new Object()};
        public final int[] integers = {31, 43, 59};
        private Thread worker;
        private Throwable failure;
        private int returned;
        private boolean armed;
        private final int expected;
        private Run(boolean warm) throws Exception {
            Class<?>[] args = {int.class, Object.class, int.class, Object.class, int.class, Object.class};
            methods = new Method[] {CompiledFrameFixture.class.getDeclaredMethod("carrier", args),
                CompiledFrameFixture.class.getDeclaredMethod("middle", args),
                CompiledFrameFixture.class.getDeclaredMethod("inner", args),
                CompiledFrameFixture.class.getDeclaredMethod("awaitGate"),
                CompiledFrameFixture.class.getDeclaredMethod("marker")};
            expected = integers[0] + integers[1] + integers[2] +
                System.identityHashCode(roots[0]) + System.identityHashCode(roots[1]) + System.identityHashCode(roots[2]);
            // Unarmed awaitGate returns normally, preserving all real calls.
            for (int i = 0; warm && i < 100000; ++i)
                if (invoke() != expected) throw new IllegalStateException("Compiled fixture warmup values changed");
        }
        private int invoke() { return carrier(integers[0], roots[0], integers[1], roots[1], integers[2], roots[2]); }
        public void start() throws Exception {
            if (armed || worker != null) throw new IllegalStateException("Compiled fixture run already started");
            retained0 = retained1 = retained2 = null;
            retainedInt0 = retainedInt1 = retainedInt2 = 0;
            if (!armGate()) throw new IllegalStateException("Compiled fixture native gate was not initialized or is already owned");
            armed = true;
            try {
                worker = new Thread(() -> {try {returned = invoke();} catch (Throwable error) {failure = error;}}, "ArtDcevmCompiledOracle");
                worker.start();
                if (!waitEntered(30000)) throw new IllegalStateException("Actual native compiled fixture worker did not enter gate");
                if (retained0 != null || retained1 != null || retained2 != null || retainedInt0 != 0 || retainedInt1 != 0 || retainedInt2 != 0)
                    throw new IllegalStateException("Compiled fixture wrote witnesses before gate release");
            } catch (Exception error) {close(); throw error;}
        }
        @Override public void close() throws Exception {
            if (!armed) return;
            releaseGate(); armed = false;
            if (worker != null) {
                worker.join(30000);
                if (worker.isAlive()) throw new IllegalStateException("Compiled fixture worker did not leave native gate");
                if (failure != null) throw new IllegalStateException("Compiled fixture worker failed", failure);
                if (returned != expected || retained0 != roots[0] || retained1 != roots[1] || retained2 != roots[2] ||
                    retainedInt0 != integers[0] || retainedInt1 != integers[1] || retainedInt2 != integers[2])
                    throw new IllegalStateException("Compiled fixture post-release values changed");
            }
        }
    }
    /** Call after native bridge initialization, outside any source stop. */
    public static Run prepare() throws Exception {return new Run(true);}
    /** Experimental cold invocation; only native actual-frame proof identifies its code kind.
     * A debuggable ART runtime may execute it in the switch interpreter, not Nterp.
     * Calling this after a warm run does not undo compilation or reset hotness. */
    public static Run prepareCold() throws Exception {return new Run(false);}
}
