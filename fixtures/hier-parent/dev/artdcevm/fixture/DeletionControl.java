package dev.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;
public final class DeletionControl {
    public static volatile CountDownLatch entered;
    public static volatile CountDownLatch release;
    public static void awaitRelease() {
        CountDownLatch latch = release;
        if (latch == null) return;
        entered.countDown();
        try { latch.await(); } catch (InterruptedException e) { throw new AssertionError(e); }
    }
}

