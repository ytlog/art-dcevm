package dev.artdcevm.fixture;

public class PrivateMethodSubject {
    private int compute() { return 31; }
    public int callOwn() { return compute(); }
}
