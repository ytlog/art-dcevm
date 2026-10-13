package com.github.ytlog.artdcevm.fixture;

/** Native declarations that fixtures/native-change deletes or turns into Java. Never registered. */
public class NativeSubject {
    public int value = 5;
    public native int nativeValue();
    public native int nativeToJava();
    public int other() { return value + 1; }
}
