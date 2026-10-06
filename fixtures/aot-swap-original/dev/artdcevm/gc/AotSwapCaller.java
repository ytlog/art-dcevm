package dev.artdcevm.gc;

import java.util.concurrent.CountDownLatch;

public class AotSwapCaller {
    public static volatile boolean release;
    public static final CountDownLatch entered = new CountDownLatch(1);
    public static AotSwapParent stale;
    public int call() {
        entered.countDown();
        while (!release) { }
        return stale.inherited();
    }
}
