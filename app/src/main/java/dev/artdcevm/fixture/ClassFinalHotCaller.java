package dev.artdcevm.fixture;

public final class ClassFinalHotCaller {
    private ClassFinalHotCaller() { }
    public static int call(ClassFinalSubject subject) { return subject.value(); }
}
