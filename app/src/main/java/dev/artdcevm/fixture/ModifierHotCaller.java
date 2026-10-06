package dev.artdcevm.fixture;

public final class ModifierHotCaller {
    private ModifierHotCaller() { }
    public static int call(ModifierSubject receiver) { return receiver.finalizable(); }
}
