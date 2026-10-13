package com.github.ytlog.artdcevm.fixture;

public final class ConstructorCaller {
    private ConstructorCaller() {}
    public static int create(int value) { return new ConstructorSubject(value).value; }
}
