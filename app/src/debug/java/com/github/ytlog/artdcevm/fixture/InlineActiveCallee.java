package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;

public final class InlineActiveCallee {
    public static final CountDownLatch entered = new CountDownLatch(1);
    public static final AtomicBoolean release = new AtomicBoolean(false);

    public int value(int input) {
        if (input < 0) {
            entered.countDown();
            while (!release.get()) Thread.yield();
            return -input;
        }
        return input + 1;
    }
}
