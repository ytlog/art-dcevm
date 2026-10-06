package dev.artdcevm.fixture;

public final class InlineActiveCaller {
    private InlineActiveCaller() {}

    public static int call(InlineActiveCallee callee, int input) {
        return callee.value(input);
    }
}
