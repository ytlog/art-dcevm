package com.github.ytlog.artdcevm.fixture;

public class PrivateStaticMethodSubject {
    static int compute() { return 73; }
    public static int callOwn() { return compute(); }
}
