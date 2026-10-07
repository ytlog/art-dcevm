package com.github.ytlog.artdcevm.fixture;

public final class PrivateFieldHotCaller {
    private PrivateFieldHotCaller() {}
    public static int read(PrivateFieldSubject subject) {
        return subject.read() + PrivateFieldSubject.readShared();
    }
}
