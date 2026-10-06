package dev.artdcevm.gc;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;

public class AotInterfaceCaller {
    public static AotInterfaceTag stale;
    public static final AtomicBoolean release = new AtomicBoolean(false);
    public static final CountDownLatch entered = new CountDownLatch(1);
    public static final AtomicBoolean defaultRelease = new AtomicBoolean(false);
    public static final CountDownLatch defaultEntered = new CountDownLatch(1);

    public int call() {
        AotInterfaceTag local = stale;
        entered.countDown();
        while (!release.get()) Thread.yield();
        return local.tag();
    }

    public int callDefault() {
        AotInterfaceTag local = stale;
        defaultEntered.countDown();
        while (!defaultRelease.get()) Thread.yield();
        return local.defaultValue();
    }
}
