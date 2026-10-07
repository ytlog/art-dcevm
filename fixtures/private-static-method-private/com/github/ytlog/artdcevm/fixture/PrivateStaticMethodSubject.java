package com.github.ytlog.artdcevm.fixture;

public class PrivateStaticMethodSubject {
    private static int compute() { return 75; }
    public static int callOwn() { return compute(); }
}
