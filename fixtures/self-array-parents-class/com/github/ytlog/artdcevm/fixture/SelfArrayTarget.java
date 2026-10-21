package com.github.ytlog.artdcevm.fixture;

public final class SelfArrayTarget implements java.io.Serializable {
    public static final SelfArrayTarget[] FIELD = null;
    public static SelfArrayTarget[] pack(SelfArrayTarget value) { return new SelfArrayTarget[]{value}; }
    public static SelfArrayTarget[][] matrix(SelfArrayTarget[] value) { return new SelfArrayTarget[][]{value}; }
    public static SelfArrayTarget[][][] cube(SelfArrayTarget[][] value) { return new SelfArrayTarget[][][]{value}; }
    public static SelfArrayTarget first(SelfArrayTarget[] value) { return value[0]; }
    public static void put(SelfArrayTarget[] values, SelfArrayTarget value) { values[0] = value; }
    public static SelfArrayTarget[] arrayCast(Object value) { return (SelfArrayTarget[]) value; }
    public static Object[] widen(SelfArrayTarget[] value) { return value; }
    public static Class<?> arrayType() { return SelfArrayTarget[].class; }
    public static void unsafeStore(Object[] values, Object value) { values[0] = value; }
    public static java.io.Serializable[] serializableView(SelfArrayTarget[] value) { return value; }
    public static Cloneable[] cloneableView(SelfArrayTarget[] value) { return (Cloneable[]) (Object) value; }
    public static java.io.Serializable[][] serializableMatrixView(SelfArrayTarget[][] value) { return value; }
    public static Cloneable[][] cloneableMatrixView(SelfArrayTarget[][] value) { return (Cloneable[][]) (Object) value; }
    public static java.io.Serializable[][][] serializableCubeView(SelfArrayTarget[][][] value) { return value; }
    public static Cloneable[][][] cloneableCubeView(SelfArrayTarget[][][] value) { return (Cloneable[][][]) (Object) value; }
    public static int stable() { return 23; }
    public static SelfArrayTarget[] constructed() { return new SelfArrayTarget[]{new SelfArrayTarget()}; }
    public int zzValue() { return 29; }
}
