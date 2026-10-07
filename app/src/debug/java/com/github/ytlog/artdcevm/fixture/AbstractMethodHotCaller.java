package com.github.ytlog.artdcevm.fixture;

public final class AbstractMethodHotCaller {
    private AbstractMethodHotCaller() { }
    public static int call(AbstractMethodSubject subject) { return subject.compute(); }
}
