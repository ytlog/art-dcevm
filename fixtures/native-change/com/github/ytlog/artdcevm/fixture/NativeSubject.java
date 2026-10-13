package com.github.ytlog.artdcevm.fixture;

public class NativeSubject {
    public int value = 5;
    public int nativeToJava() { return value + 90; }
    public int other() { return value + 1; }
}
