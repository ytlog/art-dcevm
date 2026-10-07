package com.github.ytlog.artdcevm.internal;

/**
 * 字段存储与 GC 操作的预定义校准类，仅供 Native 初始化阶段使用。
 *
 * <p>覆盖各种基本类型、对象/数组引用、静态字段和 volatile 字段，
 * 用真实对象验证字段读写、引用编码、写屏障及 monitor 相关操作。
 * 浮点 NaN 与高位整数用于检查原始位模式，不能随意替换成普通数值。
 *
 * <p>Native 校准负责恢复被修改的静态存储。字段名称、类型与初值是固定约定，
 * 修改须同步 Native 实现与校准验证；此类不是业务替换或测试请求封装。
 */
final class CalibrationFixture {
    /** 保留可解析的实例方法，便于对照成员与所属类身份。 */
    public int compute() { return intValue + 1; }

    // 实例存储：基本类型、引用与 volatile 访问路径。
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

    // 静态存储：与实例类型对应，校准结束后必须恢复原值。
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
