package com.github.ytlog.artdcevm.fixture;

public final class ClassAbstractHotCaller {
    private ClassAbstractHotCaller() { }
    public static int call(ClassAbstractSubject subject) { return subject.value(); }
}
