package com.github.ytlog.artdcevm.fixture;

/** This caller is never redefined, so its warmed resolutions must be invalidated. */
public final class MethodCaller {
    public static int callStatic() { return MethodSubject.staticValue(); }
    public static int callVirtual(MethodSubject value) { return value.virtualValue(); }
    public static int callSignature(MethodSubject value) { return value.signatureValue(4); }
    public static int callParent(MethodSubject value) { return value.parentValue(); }
    public static int callInterface(MethodDefault value) { return value.interfaceValue(); }
}
