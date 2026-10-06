package dev.artdcevm.fixture;
public class NativeShape {
    public Object marker;
    public static Object shared;
    public static native long wide(long x, double y, Object token);
    public native Object reference(Object token);
    public static native Object staticReference(Object token);
}
