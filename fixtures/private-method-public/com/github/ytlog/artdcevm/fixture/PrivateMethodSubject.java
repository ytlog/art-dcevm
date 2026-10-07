package com.github.ytlog.artdcevm.fixture;

public class PrivateMethodSubject {
    public int compute() { return 40; }
    public int callOwn() { return compute(); }
}
