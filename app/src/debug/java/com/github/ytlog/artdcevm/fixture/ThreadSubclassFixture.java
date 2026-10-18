package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

/** An application subclass of Thread; ART refuses only Thread and its supertypes. */
public class ThreadSubclassFixture extends Thread {
    public final CountDownLatch started = new CountDownLatch(1);
    public final CountDownLatch release = new CountDownLatch(1);
    public int kept;
    public Thread observedSelf;
    public boolean interrupted;
    public int result;

    public ThreadSubclassFixture(String name) { super(name); }

    @Override public void run() {
        started.countDown();
        try {
            release.await();
        } catch (InterruptedException error) {
            interrupted = true;
        }
        observedSelf = Thread.currentThread();
        result = value();
    }

    public int value() { return 1; }
}
