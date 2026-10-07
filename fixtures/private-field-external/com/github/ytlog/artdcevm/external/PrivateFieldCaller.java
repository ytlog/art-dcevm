package com.github.ytlog.artdcevm.external;

import com.github.ytlog.artdcevm.fixture.PrivateFieldSubject;

public class PrivateFieldCaller {
    public static int read(PrivateFieldSubject subject) {
        return subject.value + PrivateFieldSubject.shared;
    }
    public static void write(PrivateFieldSubject subject, int value, int shared) {
        subject.value = value;
        PrivateFieldSubject.shared = shared;
    }
}
