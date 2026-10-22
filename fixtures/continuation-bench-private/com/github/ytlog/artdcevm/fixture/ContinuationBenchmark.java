package com.github.ytlog.artdcevm.fixture;
import java.util.concurrent.CountDownLatch;
/** Both controls time an obsolete, deoptimized frame with native PC delivery. */
public class ContinuationBenchmark {
    public static boolean profiling;
    public static native void profileEnable();
    public static native void profileBegin();
    public static native void profileEnd();
    public int added;
    private int target() { return 7; }
    public long[] await(CountDownLatch ready, CountDownLatch resume, int count) throws Exception {
        ready.countDown(); resume.await();
        int sum = 0;
        for (int i = 0; i < 10000; ++i) sum += target();
        if (profiling) profileBegin();
        long started = System.nanoTime();
        for (int i = 0; i < count; ++i) sum += target();
        long elapsed = System.nanoTime() - started;
        if (profiling) profileEnd();
        return new long[]{elapsed, sum};
    }
    public interface Contract {
        int ADDED = 1;
        private int target() { return 7; }
        default long[] await(CountDownLatch ready, CountDownLatch resume, int count) throws Exception {
        ready.countDown(); resume.await();
        int sum = 0;
        for (int i = 0; i < 10000; ++i) sum += target();
        if (profiling) profileBegin();
        long started = System.nanoTime();
        for (int i = 0; i < count; ++i) sum += target();
        long elapsed = System.nanoTime() - started;
        if (profiling) profileEnd();
        return new long[]{elapsed, sum};
    }
    }
    public static final class Impl implements Contract {}
}
