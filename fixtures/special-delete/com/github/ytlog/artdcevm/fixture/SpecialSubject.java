package com.github.ytlog.artdcevm.fixture;
public class SpecialSubject {
    public int value;
    public static int initialized = 17;
    public SpecialSubject() { value = 23; }
    public native int nativeKept();
    public int compute() { return value + 2; }
}
