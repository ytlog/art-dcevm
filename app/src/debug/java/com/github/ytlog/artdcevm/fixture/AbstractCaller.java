package com.github.ytlog.artdcevm.fixture;

/** invoke-virtual through the abstract declaration. */
public final class AbstractCaller {
    private AbstractCaller() {}
    public static int abstractValue(AbstractMethodSubject subject) { return subject.abstractValue(); }
}
