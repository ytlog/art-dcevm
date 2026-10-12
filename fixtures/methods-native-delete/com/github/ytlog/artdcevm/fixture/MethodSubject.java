package com.github.ytlog.artdcevm.fixture;

import java.util.concurrent.CountDownLatch;

public class MethodSubject extends MethodParent implements MethodDefault {
    public int value = 20;
    public long keep = 30;
    public Object object = new Object();
    public static int staticKeep = 40;
    public static Object staticObject = new Object();
    private int directValue() { return value + 1; }
    public int throughDirect() { return directValue(); }
    public static int staticValue() { return staticKeep + 2; }
    public int virtualValue() { return value + 3; }
    public int signatureValue(int delta) { return value + delta; }
    public int waitThenRetire(CountDownLatch entered, CountDownLatch release) throws InterruptedException {
        entered.countDown();
        release.await();
        return value + 9;
    }
    @Override public int parentValue() { return value + 4; }
    @Override public int interfaceValue() { return value + 5; }
    public int stableValue() { return value + 6; }
}
