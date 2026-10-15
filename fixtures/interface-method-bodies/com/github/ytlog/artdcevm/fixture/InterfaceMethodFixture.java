package com.github.ytlog.artdcevm.fixture;

public final class InterfaceMethodFixture {
    public interface Contract {
        default int value() { return hidden() + 2; }
        private int hidden() { return 32; }
        static int staticValue() { return 42; }
    }
    public static class Implementation implements Contract {
        public int marker = 9;
    }
}
