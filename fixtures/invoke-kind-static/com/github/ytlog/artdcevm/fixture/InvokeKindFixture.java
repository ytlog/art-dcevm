package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

/** Old bytecode must recheck invoke kinds after a structural transaction. */
public final class InvokeKindFixture {
    public static class Subject {
        public int marker = 7;
        public static int entered;
        public int await(CountDownLatch ready, CountDownLatch resume) throws InterruptedException {
            ready.countDown(); resume.await(); return convert(1);
        }
        public static int convert(int value) { entered++; return 100 + value; }
        public static int range(long a, long b, long c) { entered++; return 100 + (int) (a + b + c); }
    }
    public static class Child extends Subject {
        public int throughSuper() { return Subject.convert(1); }
    }
    public static class CallerBase { public int keep = 11; }
    public static class Caller extends CallerBase {
        public static int hotFailures(Subject value, int count) {
            int failures = 0;
            for (int i = 0; i < count; i++) {
                try { call(value); } catch (IncompatibleClassChangeError expected) { failures++; }
            }
            return failures;
        }
        public static int awaitTarget(Subject value, CountDownLatch ready, CountDownLatch resume)
                throws InterruptedException { return value.await(ready, resume); }
        public static int call(Subject value) { return value.convert(1); }
        public static int inherited(Child value) { return value.convert(1); }
        public static int range(Subject value) { return value.range(1L, 2L, 3L); }
        public static int await(Subject value, CountDownLatch ready, CountDownLatch resume)
                throws InterruptedException {
            ready.countDown();
            resume.await();
            return value.convert(1);
        }
    }
}
