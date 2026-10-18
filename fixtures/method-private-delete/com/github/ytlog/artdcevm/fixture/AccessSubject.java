package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

/** A separate caller loses access when these virtual methods become private. */
public class AccessSubject {
    public int value = 5;
    public static int entered;
    public int callTarget() { return value + 50; }
    public int await(CountDownLatch ready, CountDownLatch resume) throws InterruptedException {
        ready.countDown(); resume.await(); return value + 50;
    }
}
