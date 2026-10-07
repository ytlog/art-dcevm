package com.github.ytlog.artdcevm.fixture;
public final class SpecialCaller {
    public static SpecialSubject constructorHot(int x) { return new SpecialSubject(x); }
    public static SpecialSubject constructorCold(int x) { return new SpecialSubject(x); }
    public static int nativeHot(SpecialSubject x) { return x.nativeGone(4); }
    public static int nativeCold(SpecialSubject x) { return x.nativeGone(5); }
    public static long staticHot() { return SpecialSubject.nativeStatic(6L); }
    public static long staticCold() { return SpecialSubject.nativeStatic(7L); }
}
