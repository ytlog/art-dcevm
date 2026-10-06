package dev.artdcevm.gc;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;

public class AotActiveSubject {
    public static AtomicBoolean release = new AtomicBoolean(false);
    public static CountDownLatch entered = new CountDownLatch(1);

    public int value() { return 2; }
}
