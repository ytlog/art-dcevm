package com.github.ytlog.artdcevm.fixture;

public class AccessSubject {
    public int value = 5;
    private int target() { return value + 20; }
    public int callTarget() { return target(); }
}
