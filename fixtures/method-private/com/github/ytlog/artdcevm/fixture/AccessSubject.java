package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

/** A separate caller loses access when these virtual methods become private. */
public class AccessSubject {
    public int value = 5;
    public static int entered;
    private int target() { entered++; return value + 20; }
    private int range(long a, long b, long c) { entered++; return value + 30 + (int) (a + b + c); }
    public int callTarget() { return target(); }
    public int await(CountDownLatch ready, CountDownLatch resume) throws InterruptedException {
        ready.countDown(); resume.await(); return target();
    }
}
