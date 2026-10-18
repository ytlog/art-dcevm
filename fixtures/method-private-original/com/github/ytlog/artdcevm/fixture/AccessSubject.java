package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

/** A separate caller loses access when these virtual methods become private. */
public class AccessSubject {
    public int value = 5;
    public static int entered;
    public int target() { return value + 2; }
    public int range(long a, long b, long c) { return value + (int) (a + b + c); }
    public int callTarget() { return target(); }
    public int await(CountDownLatch ready, CountDownLatch resume) throws InterruptedException {
        ready.countDown(); resume.await(); return target();
    }
}
