package com.github.ytlog.artdcevm.fixture;
public class SpecialSubject {
    public int value;
    public SpecialSubject() { value = 23; }
    public native int nativeKept();
    public int compute() { return value + 3; }
}
