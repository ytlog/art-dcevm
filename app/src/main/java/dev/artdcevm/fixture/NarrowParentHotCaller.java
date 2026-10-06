package dev.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

public final class NarrowParentHotCaller {
    private NarrowParentHotCaller() {}
    public static int invoke(NarrowParent parent) { return parent.inherited(); }
    public static int read(NarrowParent parent) { return parent.ancestor; }
    public static void write(NarrowParent parent, int value) { parent.ancestor = value; }
    public static volatile boolean spinRelease = true;
    public static volatile CountDownLatch spinEntered = new CountDownLatch(1);
    public static int invokeAfterGate(NarrowParent parent) {
        spinEntered.countDown();
        while (!spinRelease) { }
        return parent.inherited();
    }
}
