package com.github.ytlog.artdcevm.fixture;

public final class PrivateMethodHotCaller {
    private PrivateMethodHotCaller() {}
    public static int call(PrivateMethodSubject subject) { return subject.callOwn(); }
}
