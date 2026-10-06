package dev.artdcevm.fixture;
public class NativeShape {
    public Object marker;
    public static Object shared;
    public static long wide(long x, double y, Object token) { return x + (long) y + (token == null ? 200 : 201); }
    public Object reference(Object token) { return marker; }
    public static Object staticReference(Object token) { return shared; }
}
