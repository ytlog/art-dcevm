package com.github.ytlog.artdcevm.fixture;

public final class InterfaceEvolutionFixture {
    public interface Contract {
        private int directValue() { return 61; } private int directValue(int value) { return 160 + value; }
        static int staticValue() { return 82; } static int staticValue(int value) { return 180 + value; }
        default int virtualValue() { return 63; }
        default int signatureValue(int value) { return 60 + value; }
        default int throughDirect() { return directValue(); }
        default int throughStatic() { return staticValue(); }
        default int added() { return hiddenAdded() + staticAdded(); }
        private int hiddenAdded() { return 100; }
        private static int staticAdded() { return 10; }
        static int publicAdded() { return 12; }
        static int packageAdded() { return 13; }
        static int protectedAdded() { return 14; }
        static int syntheticAdded() { return 15; }
        static int recipePadding() { return 0; }
    }
}
