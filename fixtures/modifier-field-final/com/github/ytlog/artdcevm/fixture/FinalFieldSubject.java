package com.github.ytlog.artdcevm.fixture;

public class FinalFieldSubject {
    public final int value = 41;
    public static final int staticValue = 52;
    public int read() { return value; }
    public static int readStatic() { return staticValue; }
}
