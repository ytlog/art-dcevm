package com.github.ytlog.artdcevm.fixture;

public final class InterfaceBatchFixture {
    public interface Contract {
        default int value() { return 11; }
    }
    public static class Implementation implements Contract {
        public int marker = 17;
        public int ownValue() { return marker + 31; }
    }
    public static class Child extends Implementation {
        public int childMarker = 29;
        public int childValue() { return childMarker + 31; }
    }
    public static class Unrelated {
        public int marker = 19;
        public int value() { return 21; }
    }
}
