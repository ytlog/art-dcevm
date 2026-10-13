package com.github.ytlog.artdcevm.fixture;

public abstract class AbstractMethodSubject {
    public static int marker = 17;
    public abstract int abstractValue();
    public int concreteValue() { return 17; }
}
