package dev.artdcevm.fixture;
public class StorageSubject {
    public int anchor = 71;
    public static int value = 103;
    public long count = 8000000009L;
    public static Object reference = new Object();
    public Object shared = new Object();
    public static int change(int x) { return x + 200; }
    public long alternate(long x) { return anchor + x + 2000; }
    public static native int nativeFlip(int x);
    public int stable() { return anchor; }
}
