package com.github.ytlog.artdcevm.fixture;

public final class ModifierHotCaller {
    private ModifierHotCaller() { }
    public static int call(ModifierSubject receiver) { return receiver.finalizable(); }
}
