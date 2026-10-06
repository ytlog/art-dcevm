package dev.artdcevm.fixture;

public class PrivateStaticMethodSubject {
    public static int compute() { return 74; }
    public static int callOwn() { return compute(); }
}
