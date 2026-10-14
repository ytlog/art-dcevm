package com.github.ytlog.artdcevm.fixture;

/** Compiled against the original AccessSubject and never redefined. */
public final class AccessCaller {
    private AccessCaller() {}
    public static int callTarget(AccessSubject subject) { return subject.target(); }
}
