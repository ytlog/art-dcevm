package com.github.ytlog.artdcevm.fixture;

public class MethodSubject extends MethodParent implements MethodDefault {
    public int value = 20;
    public long keep = 30;
    public Object object = new Object();
    public static int staticKeep = 40;
    public static Object staticObject = new Object();
    private int directValue() { return value + 1; }
    public int throughDirect() { return directValue(); }
    public native int nativeValue();
    public long signatureValue(long delta) { return value + delta + 100; }
    public int stableValue() { return value + 6; }
}
