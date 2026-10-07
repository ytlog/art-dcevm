package com.github.ytlog.artdcevm.fixture;

public final class CombinedAbstractHotCaller {
    private CombinedAbstractHotCaller() { }
    public static int call(CombinedAbstractSubject subject) { return subject.compute(); }
}
