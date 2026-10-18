package com.github.ytlog.artdcevm.fixture;

/** Fixed JBR s1-s6 combinations, with migration sentinels. */
public final class BatchCombinationFixture {
    public static class S1 {
        public static class A { public int a = 1; public long added = 0; public int id() { return 11; } public int extra() { return 99; } }
        public static class B extends A { public int b = 2; public int id() { return 20; }  public int parentValue = 2; public int parentDispatch() { return 24; } }
        public static class C extends B { public int c = 3;  public int addedValue = 97; public int extraValue() { return 99; } }
    }
    public static class S2 {
        public static class A { public int id() { return 1; } }
        public static class B extends A { public int id() { return 2; }  public int parentValue = 2; public int parentDispatch() { return 24; } }
        public static class C extends B { public int c = 3; }
        public static class D extends E { public int d = 4; }
        public static class E extends C { public int id() { return 5; } }
    }
    public static class S3 {
        public static class A {}
        public static class B extends A implements I { public int parentValue = 2; public int parentDispatch() { return 24; } }
        public static class C extends B { public int c = 3;  public int addedValue = 97; public int extraValue() { return 99; } }
        public interface I { default int i() { return 7; } default int j() { return 8; } }
    }
    public static class S4 {
        public static class A { public int id() { return 1; } }
        public static class B extends B2 { public int b = 2; }
        public static class B2 extends A { public int id() { return 22; }  public int parentValue = 2; public int parentDispatch() { return 24; } }
        public static class C extends B { public int c = 3;  public int addedValue = 97; public int extraValue() { return 99; } }
    }
    public static class S5 {
        public static class A {}
        public static class B extends A { public int id() { return 2; }  public int parentValue = 2; public int parentDispatch() { return 24; } }
        public static class C extends B implements I { public int c = 3;  public int addedValue = 97; public int extraValue() { return 99; } }
        public interface I { default int i() { return 5; } default int j() { return 6; } }
    }
    public static class S6 {
        public static class A {}
        public static class B extends A { public int id() { return 2; }  public int parentValue = 2; public int parentDispatch() { return 24; } }
        public static class Base extends B { public int base = 4;  public int addedValue = 97; public int extraValue() { return 99; } }
        public interface I { default int i() { return 9; } default int j() { return 10; } }
        public static class Impl extends Base implements I { public int impl = 5; }
    }
}
