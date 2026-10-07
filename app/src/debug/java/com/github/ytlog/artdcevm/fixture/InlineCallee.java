package com.github.ytlog.artdcevm.fixture;

public final class InlineCallee {
    private static volatile Object marker;
    public int value(int input) {
        if ((input & 1023) == 0) marker = new Object();
        return input + 1;
    }
}
