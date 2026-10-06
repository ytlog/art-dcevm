package dev.artdcevm.fixture;
public class FieldSubject {
    public int keep = 41;
    public static int staticKeep = 42;
    public long i = 77;
    public static Object si = new Object();
    public int compute() { return keep + 2; }
}
