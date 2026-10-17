package com.github.ytlog.artdcevm.fixture;

public final class InterfaceOverlapFixture {
    public interface Contract {
        default int value() { return 35; }
    }
    public static class Base {
    }
    public static class Parent extends Base {
        public int added = 19;
        public int parentValue() { return 23; }
    }
}
