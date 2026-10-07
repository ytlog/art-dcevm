package com.github.ytlog.artdcevm.fixture;

public final class InterfaceOldCaller {
    private InterfaceOldCaller() {}
    public static int call(InterfaceTag value) { return value.tag(); }
    public static int callDefault(InterfaceTag value) { return value.defaultValue(); }
    public static boolean isTag(Object value) { return value instanceof InterfaceTag; }
    public static InterfaceTag castTag(Object value) { return (InterfaceTag) value; }
}
