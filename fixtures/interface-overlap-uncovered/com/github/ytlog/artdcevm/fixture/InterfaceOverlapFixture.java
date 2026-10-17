package com.github.ytlog.artdcevm.fixture;

public final class InterfaceOverlapFixture {
    public interface Contract {
        default int value() { return 36; }
    }
    public static class Parent {
        public int added = 19;
        public long another = 29;
        public int parentValue() { return 23; }
    }
}
