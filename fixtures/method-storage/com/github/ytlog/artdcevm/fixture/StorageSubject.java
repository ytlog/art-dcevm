package com.github.ytlog.artdcevm.fixture;

public class StorageSubject {
    public int value = 5;
    public static int base = 30;
    private int directValue() { return value + 1; }
    public int staticValue() { return value + 12; }
    public static int virtualValue() { return base + 13; }
    public int signatureValue(int delta) { return value + delta; }
    public int combined() { return staticValue() + virtualValue(); }
}
