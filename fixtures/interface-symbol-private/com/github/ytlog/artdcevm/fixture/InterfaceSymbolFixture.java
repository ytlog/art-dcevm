package com.github.ytlog.artdcevm.fixture;

/** The symbolic child owner and the canonical parent declaration are distinct. */
public final class InterfaceSymbolFixture {
    public static class Result { public static java.lang.invoke.MethodHandles.Lookup lookup() { return java.lang.invoke.MethodHandles.lookup(); } }
    public interface Parent {
        default Result objectValue() { return null; }
        default int echo(Result value) { return 7; }
        default int value() { return 7; }
        default int badArgument() { throw new IllegalArgumentException(); }
    }
    public interface Child extends Parent { default int value() { return 11; } }
    public static class Receiver implements Child, Parent { public int marker = 31; }
    public static class Lost implements Child { public int marker = 37; }
    public static class Override extends Receiver { public int value() { return 17; } }
    public static class CovariantBase { public Object covariant(boolean fail) { return "base"; } }
    public static class Peer extends CovariantBase { public int value() { return 5; } @java.lang.Override public String covariant(boolean fail) { if (fail) throw new IllegalStateException(); return "after"; } public static int staticValue() { return 61; } }
}
