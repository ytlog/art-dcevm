package com.github.ytlog.artdcevm.fixture;

public class FinalFieldSubject {
    // Blank finals preserve real field reads and the original initializer set.
    // ConstantVariable initializers would erase <clinit> and fold old values.
    public final int value;
    public static final int staticValue;
    public FinalFieldSubject() { value = 41; }
    static { staticValue = 52; }
    public int read() { return value; }
    public static int readStatic() { return staticValue; }
}
