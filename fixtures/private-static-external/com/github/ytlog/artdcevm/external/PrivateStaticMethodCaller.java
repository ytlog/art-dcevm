package com.github.ytlog.artdcevm.external;

import com.github.ytlog.artdcevm.fixture.PrivateStaticMethodSubject;

public class PrivateStaticMethodCaller {
    public static int call() { return PrivateStaticMethodSubject.compute(); }
}
