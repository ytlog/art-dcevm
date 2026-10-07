package com.github.ytlog.artdcevm.gc;

public final class AotActiveCaller {
    public int call(AotActiveSubject subject) {
        return subject.value();
    }
}
