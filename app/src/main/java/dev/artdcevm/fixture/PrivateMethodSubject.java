package dev.artdcevm.fixture;

public class PrivateMethodSubject {
    private int compute() { return 29; }
    public int callOwn() { return compute(); }
}
