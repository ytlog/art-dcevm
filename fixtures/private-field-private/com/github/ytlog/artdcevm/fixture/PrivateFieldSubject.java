package com.github.ytlog.artdcevm.fixture;

public class PrivateFieldSubject {
    private int value = 17;
    private static int shared = 19;
    public int read() { return value; }
    public void write(int next) { value = next; }
    public static int readShared() { return shared; }
    public static void writeShared(int next) { shared = next; }
}
