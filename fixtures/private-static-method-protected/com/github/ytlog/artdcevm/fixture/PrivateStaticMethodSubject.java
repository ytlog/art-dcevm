package com.github.ytlog.artdcevm.fixture;

public class PrivateStaticMethodSubject {
    protected static int compute() { return 72; }
    public static int callOwn() { return compute(); }
}
