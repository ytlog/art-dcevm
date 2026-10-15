package com.github.ytlog.artdcevm.fixture;

public final class InterfaceBatchFixture {
    public interface Contract {
        default int value() { return 16; }
    }
    public static class Implementation implements Contract {
        public int marker = 17;
        public int added = 23;
        public int ownValue() { return marker + 43; }
        public int addedValue() { return marker + added + 51; }
    }
    public static class Child extends Implementation {
        public int childMarker = 29;
        public int childValue() { return childMarker + 72; }
    }
}
