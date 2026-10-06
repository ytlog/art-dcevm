package dev.artdcevm.fixture;

public class FinalFieldSubject {
    public volatile int value = 41;
    public static volatile int staticValue = 52;
    public int read() { return value; }
    public static int readStatic() { return staticValue; }
}
