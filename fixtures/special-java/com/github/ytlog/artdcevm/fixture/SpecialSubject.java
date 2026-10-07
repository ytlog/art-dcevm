package com.github.ytlog.artdcevm.fixture;
public class SpecialSubject {
    public int value;
    public SpecialSubject(long x) { value = (int) x + 100; }
    public int nativeKept() { DeletionControl.awaitRelease(); return value + 500; }
    public int compute() { return value + 4; }
}
