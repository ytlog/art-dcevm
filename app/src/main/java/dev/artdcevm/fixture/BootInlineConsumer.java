package dev.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;

public final class BootInlineConsumer implements Consumer<Object> {
    public static final CountDownLatch entered = new CountDownLatch(1);
    public static final AtomicBoolean release = new AtomicBoolean(false);
    public volatile int last;

    @Override public void accept(Object value) {
        int input = (Integer) value;
        if (input < 0) waitForRelease();
        last = input + 1;
    }

    private static void waitForRelease() {
        entered.countDown();
        while (!release.get()) Thread.yield();
    }
}
