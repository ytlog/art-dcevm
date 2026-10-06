package dev.artdcevm.external;

import dev.artdcevm.fixture.PrivateStaticMethodSubject;

public class PrivateStaticMethodCaller {
    public static int call() { return PrivateStaticMethodSubject.compute(); }
}
