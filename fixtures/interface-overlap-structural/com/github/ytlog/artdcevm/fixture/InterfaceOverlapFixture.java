package com.github.ytlog.artdcevm.fixture;

public final class InterfaceOverlapFixture {
    public interface Contract {
        default int value() { return 33; }
    }
    public static class Parent {
        public int added = 19;
        public int parentValue() { return 23; }
    }
}
