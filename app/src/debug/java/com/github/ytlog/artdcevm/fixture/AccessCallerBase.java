package com.github.ytlog.artdcevm.fixture;

public class AccessCallerBase {
    public int kept = 11;
    public static int await(java.util.function.IntSupplier action,
            java.util.concurrent.CountDownLatch ready,
            java.util.concurrent.CountDownLatch resume) throws InterruptedException {
        ready.countDown(); resume.await(); return action.getAsInt();
    }
}
