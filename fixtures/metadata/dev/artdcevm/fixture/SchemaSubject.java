package dev.artdcevm.fixture;
public class SchemaSubject {
    public boolean z;
    public byte b;
    public char c;
    public short s;
    public int i;
    public long j;
    public float f;
    public double d;
    public Object reference;
    public int[][] matrix;
    public static String 中文 = "metadata";
    public synchronized int synchronizedMethod() { return i; }
    public void overloaded(int value) { i = value; }
    public long overloaded(long value) { return value; }
    public Object[][] arrays(int[][] values, Object[][] objects) { return objects; }
    public native void nativeMethod();
}
