package com.github.ytlog.artdcevm.fixture;

public final class InlineHotCaller {
    private InlineHotCaller() {}
    public static int call(InlineCallee callee, int input) {
        return callee.value(input);
    }
}
