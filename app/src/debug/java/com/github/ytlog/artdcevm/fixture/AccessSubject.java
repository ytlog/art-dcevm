package com.github.ytlog.artdcevm.fixture;

/** fixtures/method-private makes target private. */
public class AccessSubject {
    public int value = 5;
    public int target() { return value + 2; }
}
