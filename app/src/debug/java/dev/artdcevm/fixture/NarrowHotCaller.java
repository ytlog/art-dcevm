package dev.artdcevm.fixture;

public final class NarrowHotCaller {
    private NarrowHotCaller() {}
    public static int call(NarrowSubject subject) { return subject.compute(); }
}
