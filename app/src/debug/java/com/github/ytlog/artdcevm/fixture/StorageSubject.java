package com.github.ytlog.artdcevm.fixture;
public class StorageSubject {
    public int anchor = 71;
    public int value = 31;
    public static long count = 9000000007L;
    public Object reference = new Object();
    public static Object shared = new Object();
    public int change(int x) { if (x == -1) DeletionControl.awaitRelease(); return anchor + x; }
    public static long alternate(long x) { if (x == -1) DeletionControl.awaitRelease(); return x + 1000; }
    public native int nativeFlip(int x);
    public int stable() { return anchor; }
}
