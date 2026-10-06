package dev.artdcevm.fixture;
public class SpecialSubject {
    public int value;
    public static int initialized = 17;
    public SpecialSubject() { value = 23; }
    public SpecialSubject(int x) {
        value = x;
        DeletionControl.awaitRelease();
        value += 10;
    }
    public native int nativeGone(int x);
    public static native long nativeStatic(long x);
    public native int nativeKept();
    public int compute() { return value + 1; }
}
