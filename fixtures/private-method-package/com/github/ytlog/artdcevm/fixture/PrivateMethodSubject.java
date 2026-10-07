package com.github.ytlog.artdcevm.fixture;

public class PrivateMethodSubject {
    int compute() { return 43; }
    public int callOwn() { return compute(); }
}
