package com.github.ytlog.artdcevm.fixture;

public final class InterfaceMethodFixture {
    public interface Contract {
        default int value() { return hidden() + 1; }
        private int hidden() { return 31; }
        static int staticValue() { return 41; }
    }
    public static class Implementation implements Contract {
        public int marker = 9;
    }
}
