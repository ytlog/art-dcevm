package com.github.ytlog.artdcevm.fixture;

public class PrivateMethodSubject {
    protected int compute() { return 42; }
    public int callOwn() { return compute(); }
}
