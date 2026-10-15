package com.github.ytlog.artdcevm.fixture;

public final class InterfaceOverlapFixture {
    public interface Contract {
        default int value() { return 32; }
    }
    public static class Parent {
        public int parentValue() { return 22; }
    }
}
