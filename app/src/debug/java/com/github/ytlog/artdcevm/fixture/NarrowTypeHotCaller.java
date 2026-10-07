package com.github.ytlog.artdcevm.fixture;

public final class NarrowTypeHotCaller {
    private NarrowTypeHotCaller() {}

    public static boolean isParent(Object value) { return value instanceof NarrowParent; }
    public static NarrowParent castParent(Object value) { return (NarrowParent) value; }
    public static boolean isParentArray(Object value) { return value instanceof NarrowParent[]; }
    public static NarrowParent[] castParentArray(Object value) { return (NarrowParent[]) value; }
    public static int readParentArrayField(NarrowParent[] value) { return value[0].ancestor; }
    public static int invokeParentArrayMethod(NarrowParent[] value) { return value[0].inherited(); }
}
