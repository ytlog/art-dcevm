package dev.artdcevm.fixture;

public class PrivateFieldSubject {
    protected int value = 17;
    protected static int shared = 19;
    public int read() { return value; }
    public void write(int next) { value = next; }
    public static int readShared() { return shared; }
    public static void writeShared(int next) { shared = next; }
}
