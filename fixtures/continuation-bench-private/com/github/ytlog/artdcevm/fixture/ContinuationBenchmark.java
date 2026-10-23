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
    private int directTarget() { return 7; }
    public long[] awaitDirect(CountDownLatch ready, CountDownLatch resume, int count) throws Exception {
        return directBody(ready, resume, count);
    }
    private long[] directBody(CountDownLatch ready, CountDownLatch resume, int count) throws Exception {
        ready.countDown(); resume.await();
        int sum = 0;
        for (int i = 0; i < 10000; ++i) sum += directTarget();
        long started = System.nanoTime();
        for (int i = 0; i < count; ++i) sum += directTarget();
        return new long[]{System.nanoTime() - started, sum};
    }
    public interface Contract {
        int ADDED = 1;
    private int directTarget() { return 7; }
    default long[] awaitDirect(CountDownLatch ready, CountDownLatch resume, int count) throws Exception {
        return directBody(ready, resume, count);
    }
    private long[] directBody(CountDownLatch ready, CountDownLatch resume, int count) throws Exception {
        ready.countDown(); resume.await();
        int sum = 0;
        for (int i = 0; i < 10000; ++i) sum += directTarget();
        long started = System.nanoTime();
        for (int i = 0; i < count; ++i) sum += directTarget();
        return new long[]{System.nanoTime() - started, sum};
    }
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
