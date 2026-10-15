package com.github.ytlog.artdcevm.fixture;

public final class PointerIdFixture {
    public interface Contract {
        default int pointerValue() { return 52; }
    }
    public static class Implementation implements Contract {
        public int marker = 17;
        public static int staticMarker = 19;
    }
    public static class Child extends Implementation { }
}
