package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

/** Obsolete own frames must select the evolved private declaration directly. */
public final class PrivateSelfFixture {
    private PrivateSelfFixture() {}
    public static class Subject {
        public int value = 5;
        public static final java.util.concurrent.atomic.AtomicInteger entered = new java.util.concurrent.atomic.AtomicInteger();
        public int target() { return value + 2; }
        public int range(long a, long b, long c) { return value + (int) (a + b + c); }
        public int await(CountDownLatch ready, CountDownLatch resume, int mode) throws InterruptedException {
            ready.countDown(); resume.await();
            if (mode == 1) return range(1L, 2L, 3L);
            Subject receiver = mode == 2 ? null : this;
            return receiver.target();
        }
    }
    public static class Child extends Subject {
        public int target() { return 47; }
    }
    public interface Contract {
        default int target() { return 11; }
        default int range(long a, long b, long c) { return (int) (a + b + c); }
        default int await(CountDownLatch ready, CountDownLatch resume, int mode) throws InterruptedException {
            ready.countDown(); resume.await();
            if (mode == 1) return range(1L, 2L, 3L);
            Contract receiver = mode == 2 ? null : this;
            return receiver.target();
        }
    }
    public static class Impl implements Contract {}
    public static class Override implements Contract {
        public int target() { return 49; }
    }
}
