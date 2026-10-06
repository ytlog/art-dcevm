package dev.artdcevm.fixture;
public class SpecialSubject {
    public int value;
    public SpecialSubject(long x) { value = (int) x + 100; }
    public native int nativeKept();
    public int compute() { return value + 4; }
}
