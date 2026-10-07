package com.github.ytlog.artdcevm.platform.android13;

/** Private bootstrap fixture; JNI calibration restores its static storage. */
final class CalibrationFixture {
    public int compute() { return intValue + 1; }
    public boolean booleanValue = true;
    public byte byteValue = -91;
    public char charValue = '\u8123';
    public short shortValue = -19001;
    public int intValue = 0x81234567;
    public long longValue = 0xFEDCBA9876543210L;
    public float floatValue = Float.intBitsToFloat(0xFFC12345);
    public double doubleValue = Double.longBitsToDouble(0xFFF8123456789ABCL);
    public Object referenceValue = new Object();
    public Object[] arrayValue = new Object[1];
    public volatile Object[] volatileArrayValue = new Object[1];
    public volatile int volatileIntValue = -23;
    public volatile long volatileLongValue = 0x8765432101234567L;
    public volatile Object volatileReferenceValue = new Object();
    public static boolean staticBooleanValue = true;
    public static byte staticByteValue = -91;
    public static char staticCharValue = '\u8123';
    public static short staticShortValue = -19001;
    public static int staticIntValue = 0x81234567;
    public static long staticLongValue = 0xFEDCBA9876543210L;
    public static float staticFloatValue = Float.intBitsToFloat(0xFFC12345);
    public static double staticDoubleValue = Double.longBitsToDouble(0xFFF8123456789ABCL);
    public static Object staticReferenceValue = new Object();
    public static Object[] staticArrayValue = new Object[1];
    public static volatile Object[] staticVolatileArrayValue = new Object[1];
    public static volatile int staticVolatileIntValue = -23;
    public static volatile long staticVolatileLongValue = 0x8765432101234567L;
    public static volatile Object staticVolatileReferenceValue = new Object();
}
