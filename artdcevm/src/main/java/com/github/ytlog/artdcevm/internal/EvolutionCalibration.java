package com.github.ytlog.artdcevm.internal;

/**
 * 完整演进初始化使用的预定义成员校准类，由 Native 初始化入口构造。
 *
 * <p>Native 层通过公开 JNI/JVMTI 观察与自有读取结果对照，验证真实类、字段、
 * 方法、成员 ID 和相关运行时访问。它是生产初始化的一部分，保留在库主源码中。
 * 字段名称、类型、初值及 compute 方法构成校准约定；修改时必须同步 Native 校准实现。
 */
final class EvolutionCalibration {
    // 覆盖实例 int/long 和静态字段，使用明确的非零值核对读取结果。
    public int value = 37;
    public long wide = 0x123456789ABCDEFL;
    public static int counter = 7;
    /** 提供可通过反射/JNI 解析的真实实例方法，返回值依赖已校准字段。 */
    public int compute() { return value + 1; }
    /** 校准同包其他类访问 private 成员时的 IllegalAccessError。 */
    private int privateAccessCalibration() { return value; }
    /** 校准 static 调用类别，不修改已校准存储。 */
    public static int staticInvokeCalibration() { return 7; }
    /** 校准带数组/参数列表及非 int 返回值的 Signature 异常格式化。 */
    public long signatureLookupCalibration(String[] values, int count) { return count; }
    interface InvocationContract { int invoke(); }
}
